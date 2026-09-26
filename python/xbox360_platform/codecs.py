"""Checked Xbox boundaries for the portable Xiph codec sources."""
import re

VERSIONS = {'opus': '1.6.1', 'opusfile': '0.12+20221121', 'ogg': '1.3.6', 'vorbis': '1.3.7'}


def replace(text, old, new, count=1):
    if text.count(old) != count:
        raise ValueError('Codec source form changed: '+old)
    return text.replace(old, new)


def adapt(package, version, relative, text):
    if VERSIONS.get(package) != version:
        raise ValueError('Unreviewed codec version: '+package+' '+version)
    if package == 'opus' and relative == 'celt/arch.h':
        text = replace(text, '#if defined(_MSC_VER)\n   _set_abort_behavior',
                       '#if defined(_MSC_VER) && !defined(_XBOX)\n   _set_abort_behavior')
    elif package == 'opus' and relative == 'celt/ecintrin.h':
        text = replace(text, '#if defined(_MSC_VER) && (_MSC_VER >= 1400)',
                       '#if defined(_MSC_VER) && (_MSC_VER >= 1400) && !defined(_XBOX)')
    elif package == 'opusfile' and relative == 'src/stream.c':
        # XDK stdio exposes descriptor status and narrow filesystem paths.
        text = replace(text, '# include <windows.h>',
                       '#ifdef _XBOX\n# include <sys/stat.h>\n#else\n# include <windows.h>\n#endif')
        desktop = ('  intptr_t h_file;\n  h_file=_get_osfhandle(_fileno(_fp));\n'
                   '  if(h_file!=-1\n   &&(GetFileType((HANDLE)h_file)&~FILE_TYPE_REMOTE)==FILE_TYPE_DISK){')
        text = replace(text, desktop, '#ifdef _XBOX\n  struct _stat status;\n'
                       '  if(_fstat(_fileno(_fp),&status)==0 && (status.st_mode&_S_IFMT)==_S_IFREG){\n'
                       '#else\n'+desktop+'\n#endif')
        text = replace(text, '#if !defined(_WIN32)', '#if !defined(_WIN32) || defined(_XBOX)', 2)
    return text


def units(package, read):
    if package == 'ogg':
        return ['src/bitwise.c', 'src/framing.c']
    if package == 'opusfile':
        return ['src/info.c', 'src/internal.c', 'src/opusfile.c', 'src/stream.c']
    result = []
    if package == 'opus':
        for file, sections in (('opus_sources.mk', ('OPUS_SOURCES', 'OPUS_SOURCES_FLOAT')),
                               ('celt_sources.mk', ('CELT_SOURCES',)),
                               ('silk_sources.mk', ('SILK_SOURCES', 'SILK_SOURCES_FLOAT'))):
            text = read(file).replace('\\\n', ' ')
            for section in sections:
                matches = re.findall(r'^'+section+r'\s*=\s*([^\n]+)', text, re.M)
                if len(matches) != 1:
                    raise ValueError('Codec source manifest changed: '+section)
                result.extend(matches[0].split())
    elif package == 'vorbis':
        text = read('lib/CMakeLists.txt')
        for section in ('VORBIS_SOURCES', 'VORBISFILE_SOURCES', 'VORBISENC_SOURCES'):
            matches = re.findall(r'set\('+section+r'\s+([^)]*)\)', text)
            if len(matches) != 1:
                raise ValueError('Vorbis source manifest changed: '+section)
            result.extend('lib/'+name for name in matches[0].split())
    else:
        raise ValueError('Unknown codec: '+package)
    if len(set(result)) != len(result) or not all(re.fullmatch(r'[\w/]+\.c', name) for name in result):
        raise ValueError('Unexpected codec source inventory')
    return result
