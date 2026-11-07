"""Adapt pinned libzip 1.11.4 sources for the Xbox 360 toolchain."""

import hashlib
import re
import subprocess
from pathlib import Path


VERSION = '1.11.4'
_EXTRA_UNITS = (
    'zip_err_str.c', 'zip_source_file_win32.c', 'zip_source_file_win32_named.c',
    'zip_source_file_win32_ansi.c', 'zip_source_file_win32_utf8.c',
    'zip_source_file_win32_utf16.c', 'zip_random_win32.c',
)
_ENABLED = set('ENABLE_FDOPEN HAVE__CLOSE HAVE__DUP HAVE__FDOPEN HAVE__FILENO '
               'HAVE__FSEEKI64 HAVE__FSTAT64 HAVE__SETMODE HAVE__SNPRINTF '
               'HAVE__SNPRINTF_S HAVE__SNWPRINTF_S HAVE__STAT64 HAVE__STRDUP '
               'HAVE__STRICMP HAVE__STRTOI64 HAVE__STRTOUI64 HAVE__UNLINK '
               'HAVE_LOCALTIME_S HAVE_MEMCPY_S HAVE_STRERROR_S HAVE_STRNCPY_S'.split())


def _once(text, before, after):
    if text.count(before) != 1:
        raise ValueError('libzip projection anchor drift: ' + before)
    return text.replace(before, after)


def transform(name, text):
    if name == 'zip_dirent.c':
        text = _once(text, '    zip_uint16_t version_needed;', '')
        text = _once(text, '    zip_int16_t version_madeby = 63 | (de->version_madeby & 0xff00);',
                     '    version_madeby = 63 | (de->version_madeby & 0xff00);')
        text = _once(text, '    bool has_changed = false;',
                     '    bool has_changed = false;\n    zip_uint16_t version_needed;\n    zip_int16_t version_madeby;')
    elif name == 'zip_source_file_win32.h':
        text = _once(text, '#include <windows.h>',
                     '#ifdef _XBOX\n#include <xtl.h>\n#else\n#include <windows.h>\n#endif')
        text = _once(text, '#include <aclapi.h>',
                     '#ifdef HAVE_GETSECURITYINFO\n#include <aclapi.h>\n#endif')
    elif name == 'zip_source_file_win32.c':
        start = text.index('    return zip_source_file_common_new(NULL, h,')
        end = text.index('\n}', start)
        text = text[:start] + '    zip_error_set(error, ZIP_ER_OPNOTSUPP, 0);\n    return NULL;' + text[end:]
        start = text.index('    if (GetFileType(h) == FILE_TYPE_DISK)')
        end = text.index('\n    /* TODO:', start)
        text = text[:start] + '    zip_error_set(&ctx->error, ZIP_ER_OPNOTSUPP, 0);\n    return false;\n' + text[end:]
    elif name == 'zip_source_file_win32_named.c':
        text = text.replace('INVALID_FILE_ATTRIBUTES', '((DWORD)-1)')
        text = _once(text, '    PSECURITY_DESCRIPTOR psd = NULL;', '    void *psd = NULL;')
        start = text.index('            if (file_attributes.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)')
        end = text.index('\n        }', start)
        text = text[:start] + '            st->regular_file = true;' + text[end:]
        text = text.replace('(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE)', 'FILE_ATTRIBUTE_DIRECTORY')
        text = _once(text, '    attributes = file_ops->get_file_attributes(ctx->tmpname);',
                     '    ctx->fout = NULL;\n    attributes = file_ops->get_file_attributes(ctx->tmpname);')
    elif name == 'zip_source_file_win32_utf8.c':
        start = text.index('    int size;', text.index('zip_source_file_create('))
        text = text[:start] + '''    char *resolved;
    zip_source_t *source;
    if (fname == NULL || length < ZIP_LENGTH_UNCHECKED) {
        zip_error_set(error, ZIP_ER_INVAL, 0);
        return NULL;
    }
    resolved = shipyard_zip_resolve_path(fname, error);
    if (resolved == NULL) return NULL;
    source = zip_source_win32a_create(resolved, start, length, error);
    free(resolved);
    return source;
}
'''
        text = _once(text, '#include "zip_source_file_win32.h"',
                     '#include "zip_source_file_win32.h"\n#include "libzip/native_boundary.h"')
    elif name == 'zip_source_file_win32_utf16.c':
        start = text.index('ZIP_EXTERN zip_source_t *')
        end = text.index('\n\nstatic char *', start)
        methods = text[start:end]
        old = '    return zip_source_file_common_new((const char *)fname, NULL, start, length, NULL, &_zip_source_file_win32_named_ops, &ops_utf16, error);'
        methods = _once(methods, old, '    zip_error_set(error, ZIP_ER_OPNOTSUPP, 0);\n    return NULL;')
        text = text[:text.index('#include "zip_source_file_win32.h"')] + '#include "zip_source_file_win32.h"\n' + methods + '\n'
    elif name == 'zip_random_win32.c':
        start = text.index('#include <windows.h>')
        end = text.index('#ifndef HAVE_RANDOM_UINT32')
        text = text[:start] + '''#include "libzip/native_boundary.h"
ZIP_EXTERN bool
zip_secure_random(zip_uint8_t *buffer, zip_uint16_t length) {
    return shipyard_zip_secure_random(buffer, length) != 0;
}

''' + text[end:]
    return text


def _write_stable(path, content):
    data = content.encode('utf-8')
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def public_config(template):
    config = template.replace('#cmakedefine ZIP_STATIC', '#define ZIP_STATIC')
    values = dict(libzip_VERSION=VERSION, libzip_VERSION_MAJOR='1', libzip_VERSION_MINOR='11',
                  libzip_VERSION_PATCH='4', LIBZIP_TYPES_INCLUDE='#include <stdint.h>')
    for width in (8, 16, 32, 64):
        values['ZIP_INT' + str(width) + '_T'] = 'int' + str(width) + '_t'
        values['ZIP_UINT' + str(width) + '_T'] = 'uint' + str(width) + '_t'
    for key, value in values.items():
        config = config.replace('${' + key + '}', value)
    if '${' in config or '#cmakedefine' in config:
        raise ValueError('Unresolved libzip public configuration')
    return config


def project(source: Path, output: Path, source_prefix: str):
    """Return selected units and hashes; source verification stays with the consumer."""
    source = Path(source)
    output = Path(output)
    units = re.findall(r'^  (zip_\S+\.c)$',
                       (source / 'lib/CMakeLists.txt').read_text().split('add_library(libzip::zip')[0], re.M)
    units.extend(_EXTRA_UNITS)
    pending = {}
    for path in sorted((source / 'lib').glob('*')):
        if path.suffix not in ('.h', '.c') or (path.suffix == '.c' and path.name not in units):
            continue
        origin = source_prefix.rstrip('/') + '/lib/' + path.name
        pending[path.name] = ('// Generated Shipyard XDK projection; do not edit.\n'
                              '#line 1 "' + origin + '"\n' + transform(path.name, path.read_text()))

    config = re.sub(r'^#cmakedefine (\w+).*$',
                    lambda match: '#define ' + match[1] if match[1] in _ENABLED else '/* unavailable: ' + match[1] + ' */',
                    (source / 'config.h.in').read_text(), flags=re.M)
    config = config.replace('/* unavailable: SIZEOF_OFF_T */', '#define SIZEOF_OFF_T 4')
    config = config.replace('/* unavailable: SIZEOF_SIZE_T */',
                            '#ifdef _XBOX\n#define SIZEOF_SIZE_T 4\n#define WORDS_BIGENDIAN\n#else\n#define SIZEOF_SIZE_T 8\n#endif')
    config = config.replace('@CMAKE_PROJECT_NAME@', 'libzip').replace('@CMAKE_PROJECT_VERSION@', VERSION)
    config = config.replace('/* END DEFINES */', '#ifndef _XBOX\n#define HAVE_SNPRINTF\n#endif\n/* END DEFINES */')
    pending['config.h'] = config

    pending['zipconf.h'] = public_config((source / 'zipconf.h.in').read_text())

    errors = output.parent / 'error-strings'
    errors.mkdir(parents=True, exist_ok=True)
    subprocess.run(['cmake', '-DPROJECT_SOURCE_DIR=' + str(source),
                    '-P', str(source / 'cmake/GenerateZipErrorStrings.cmake')],
                   cwd=errors, check=True, capture_output=True)
    pending['zip_err_str.c'] = (errors / 'zip_err_str.c').read_text()
    for name, content in pending.items():
        _write_stable(output / name, content)
    return {'units': units, 'outputs': {name: hashlib.sha256(content.encode('utf-8')).hexdigest()
                                       for name, content in sorted(pending.items())}}
