"""Project pinned ZAPDTR and OTRExporter sources into an in-process XDK extractor.

ZAPD parses ROM resources from the asset XML and OTRExporter serializes them; the
projection keeps only extract-directory mode and streams entries into Shipyard's
stored O2R writer. Generic passes lower C++11-20 syntax the XDK compiler lacks;
anchored edits replace process, thread, archive, and host byte-order plumbing.
Entries match desktop archives byte for byte, except fields desktop builds leave
uninitialized, which are zeroed. Source verification and game selection remain
consumer-owned.
"""

import hashlib
import re
from pathlib import Path

from . import gfxd
from .prism import lower_ranges, walk
from .cpp_aggregate import lower_assignments
from .cpp_containers import lower_table_initializers
from .cpp_literals import lower_binary_integers
from .cpp_syntax import collect_member_defaults, scoped_enums

ZAPD_REVISION = 'ee3397a365c5f350a60538c88f0643f155944836'
OTR_REVISION = '32e088e28c8cdd055d4bb8f3f219d33ad37963f3'

# Units outside extract-directory mode, or replaced by Shipyard services.
_EXCLUDED = frozenset((
    'ZAPD/ExecutableMain.cpp', 'ZAPD/CrashHandler.cpp', 'ZAPD/Overlays/ZOverlay.cpp',
    'OTRExporter/ExporterArchiveOTR.cpp', 'OTRExporter/ExporterArchiveO2R.cpp',
    'OTRExporter/ExporterArchive.cpp',
))
_FILESYSTEM = re.compile(r'#if __has_include\(<filesystem>\)\n#include <filesystem>\n'
                         r'namespace fs = std::filesystem;\n#else\n#include <experimental/filesystem>\n'
                         r'namespace fs = std::experimental::filesystem;\n#endif\n')


def _once(text, before, after):
    if text.count(before) != 1:
        raise ValueError('ZAPD projection anchor drift: ' + before[:100])
    return text.replace(before, after)


def _cut(text, start, end):
    """Remove text from the start anchor up to, not including, the end anchor."""
    first = text.index(start)
    return text[:first] + text[text.index(end, first):]


def _otr_includes(text):
    # Libultraship supplies only resource identifiers to the exporters.
    text = text.replace('#include <libultraship/bridge.h>\n', '')
    text = text.replace('#include <ship/resource/Resource.h>', '#include <lus/ShipResourceType.h>')
    text = text.replace('#include <fast/resource/ResourceType.h>', '#include <lus/FastResourceType.h>')
    for game in ('"../../mm/2s2h/resource/type/2shResourceType.h"', '"../../soh/soh/resource/type/SohResourceType.h"'):
        text = text.replace('#include ' + game, '#include <lus/GameResourceType.h>')
    for game in ('<resource/type/Animation.h>', '"../../soh/soh/resource/type/Animation.h"'):
        text = text.replace('#include ' + game, '#include <lus/GameAnimation.h>')
    # Exporter diagnostics go to the extraction log; spdlog is a desktop dependency.
    text = text.replace('#include "spdlog/spdlog.h"\n', '').replace('#include <spdlog/spdlog.h>\n', '')
    text = re.sub(r'\bspdlog::(error|warn|info)\(', r'ShipyardZapdLog(', text)
    text = text.replace('.contains(', '.count(')
    return text


MERGE_VERTEX_LISTS = """void ZDisplayList::MergeConnectingVertexLists()
{
	if (vertices.empty())
		return;
	std::map<uint32_t, std::vector<ZVtx> >::iterator last = vertices.begin();
	std::map<uint32_t, std::vector<ZVtx> >::iterator current = last;
	for (++current; current != vertices.end();)
	{
		size_t lastEnd = last->first + last->second.size() * 16;
		if (lastEnd >= current->first)
		{
			for (size_t j = (lastEnd - current->first) / 16; j < current->second.size(); j++)
				last->second.push_back(current->second[j]);
			vertices.erase(current++);
		}
		else
			last = current++;
	}
}

"""

# Every resource registers its XML attributes, and display lists hold thousands of
# vertex resources, so a vector keeps construction and copies to one allocation.
RESOURCE_ATTRIBUTES = """class ResourceAttributes
{
public:
	typedef std::pair<std::string, ResourceAttribute> value_type;
	typedef std::vector<value_type>::iterator iterator;
	typedef std::vector<value_type>::const_iterator const_iterator;
	iterator begin() { return items.begin(); }
	iterator end() { return items.end(); }
	const_iterator begin() const { return items.begin(); }
	const_iterator end() const { return items.end(); }
	iterator find(const std::string& key)
	{
		for (iterator it = items.begin(); it != items.end(); ++it)
			if (it->first == key)
				return it;
		return items.end();
	}
	const_iterator find(const std::string& key) const
	{
		for (const_iterator it = items.begin(); it != items.end(); ++it)
			if (it->first == key)
				return it;
		return items.end();
	}
	size_t count(const std::string& key) const { return find(key) != end() ? 1 : 0; }
	// Same result as assigning a fresh attribute through operator[].
	void Register(const std::string& key, bool required, const std::string& value)
	{
		iterator it = find(key);
		if (it == items.end())
		{
			if (items.empty())
				items.reserve(5);
			items.push_back(value_type(key, ResourceAttribute()));
			it = items.end() - 1;
		}
		it->second.key = key;
		it->second.value = value;
		it->second.isRequired = required;
		it->second.wasSet = false;
	}
	ResourceAttribute& operator[](const std::string& key)
	{
		iterator it = find(key);
		if (it != items.end())
			return it->second;
		if (items.empty())
			items.reserve(5);
		items.push_back(value_type(key, ResourceAttribute()));
		return items.back().second;
	}
	ResourceAttribute& at(const std::string& key)
	{
		iterator it = find(key);
		if (it == items.end())
			throw std::out_of_range("unregistered resource attribute: " + key);
		return it->second;
	}
	const ResourceAttribute& at(const std::string& key) const
	{
		const_iterator it = find(key);
		if (it == items.end())
			throw std::out_of_range("unregistered resource attribute: " + key);
		return it->second;
	}

private:
	std::vector<value_type> items;
};

"""


def _native_opens(text, pattern, replacement, expected):
    text, count = re.subn(pattern, replacement, text)
    if count != expected:
        raise ValueError('ZAPD file open inventory changed: ' + str(count))
    return text


def _file_specific(name, text, parser):
    if name.startswith('OTRExporter/'):
        text = _otr_includes(text)
    # The XDK opens only native absolute paths, which the filesystem shim resolves.
    if name in ('ZAPD/GameConfig.cpp', 'ZAPD/Main.cpp'):
        text = _native_opens(text, r'doc\.LoadFile\((.+)\);', r'doc.LoadFile(ShipyardZapdNative(\1).c_str());',
                             3 if name == 'ZAPD/GameConfig.cpp' else 1)
    elif name == 'ZAPD/Utils/DiskFile.h':
        text = _native_opens(text, r'std::(i|o)fstream file\(filePath,',
                             r'std::\1fstream file(ShipyardZapdNative(filePath).c_str(),', 7)
    if name == 'ZAPD/Utils/StringHelper.h':
        for kind in ('std::vector<std::string_view> Split(std::string_view s, const std::string& delimiter)',
                     'bool IsValidHex(std::string_view str)', 'bool IsValidOffset(std::string_view str)'):
            text = _once(text, '\tstatic ' + kind + ';\n', '')
    elif name == 'ZAPD/Utils/StringHelper.cpp':
        text = _cut(text, 'std::vector<std::string_view> StringHelper::Split(std::string_view s',
                    'std::string StringHelper::Strip')
        text = _cut(text, 'bool StringHelper::IsValidHex(const std::string& str)\n',
                    'bool StringHelper::IsValidOffset(std::string_view str)')
        text = _once(text, 'bool StringHelper::IsValidHex(std::string_view str)',
                     'bool StringHelper::IsValidHex(const std::string& str)')
        text = _cut(text, 'bool StringHelper::IsValidOffset(std::string_view str)',
                    'bool StringHelper::IsValidOffset(const std::string& str)')
        text = _once(text, 'return std::equal(a.begin(), a.end(), b.begin(), b.end(),',
                     'return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),')
    elif name == 'ZAPD/ZScalar.cpp':
        text, count = re.subn(r'sizeof\(scalarData\.(\w+)\)', r'sizeof(((ZScalarData*)0)->\1)', text)
        if count != 10:
            raise ValueError('ZScalar size inventory changed: ' + str(count))
    elif name == 'ZAPD/Utils/MemoryStream.cpp':
        text = text.replace(') : MemoryStream()\n', ')\n')
    elif name == 'ZAPD/ZAudio.cpp':
        text = _once(text, 'DrumEntry drum = {0};', 'DrumEntry drum;')
    elif name == 'ZAPD/Utils/BinaryWriter.h':
        # Entry headers declare little-endian payloads on every host.
        text = _once(text, 'Endianness endianness = Endianness::Native;', 'Endianness endianness = Endianness::Little;')
    elif name == 'ZAPD/yaz0/yaz0.cpp':
        # Callers pass words loaded straight from ROM memory, so read their bytes as big-endian.
        text = _once(text, '  u8 w1 = d & 0xFF;\n  u8 w2 = (d >> 8) & 0xFF;\n  u8 w3 = (d >> 16) & 0xFF;\n'
                     '  u8 w4 = d >> 24;\n  return (w1 << 24) | (w2 << 16) | (w3 << 8) | w4;\n',
                     '  const u8* b = (const u8*)&d;\n'
                     '  return ((u32)b[0] << 24) | ((u32)b[1] << 16) | ((u32)b[2] << 8) | b[3];\n')
    elif name == 'ZAPD/ZDisplayList.cpp':
        # Instructions are native 64-bit words after parsing.
        text = _once(text, 'gfxd_endian(gfxd_endian_little, sizeof(uint64_t));', 'gfxd_endian(gfxd_endian_host, sizeof(uint64_t));')
        # Upstream copies every vertex list to walk the map; this walks it in place.
        start = text.index('void ZDisplayList::MergeConnectingVertexLists()\n{\n')
        end = text.index('void ZDisplayList::TextureGenCheck()\n')
        text = text[:start] + MERGE_VERTEX_LISTS + text[end:]
    elif name == 'ZAPD/ZResource.h':
        text = _once(text, '\tstd::map<std::string, ResourceAttribute> registeredAttributes;',
                     '\tResourceAttributes registeredAttributes;')
        text = _once(text, 'class ZResource\n{\npublic:\n', RESOURCE_ATTRIBUTES + 'class ZResource\n{\npublic:\n')
        # Some limb and room fields are never assigned; zeroed blocks match desktop archives.
        text = _once(text, 'class ZResource\n{\npublic:\n',
                     'class ZResource\n{\npublic:\n'
                     '\tstatic void* operator new(size_t size) { void* block = calloc(1, size); '
                     'if (!block) throw std::bad_alloc(); return block; }\n'
                     '\tstatic void operator delete(void* block) { free(block); }\n'
                     '\tstatic void* operator new(size_t, void* where) { return where; }\n'
                     '\tstatic void operator delete(void*, void*) {}\n')
    elif name == 'ZAPD/GameConfig.cpp':
        text = _once(text, 'using ConfigFunc = void (GameConfig::*)(const tinyxml2::XMLElement&);',
                     'typedef void (GameConfig::*ConfigFunc)(const tinyxml2::XMLElement&);')
        text = _once(text, 'std::invoke(it->second, *this, *child);', '(this->*(it->second))(*child);')
        # A relative folder names a path beside the asset folder that holds the config.
        text = _once(text, '\texternalXmlFolder = pathValue;\n',
                     '\tfs::path folder(pathValue);\n'
                     '\texternalXmlFolder = folder.is_absolute() ? folder : '
                     'fs::path(configFilePath).parent_path().parent_path() / folder;\n')
        # The texture pool names C include files only; skipping it skips a CRC of every texture.
        text = _once(text, '\tstd::string fileName = element.Attribute("File");\n'
                     '\tReadTexturePool(Path::GetDirectoryName(configFilePath) / fileName);\n', '')
    elif name == 'ZAPD/Main.cpp':
        text = _zapd_main(text)
    elif name == 'ZAPD/ZRom.h':
        text = _once(text, '    bool IsMQ();', '    bool IsMQ();\n'
                     '    uint32_t GetVersionCrc() const\n    {\n'
                     '        return ((uint32_t)romData[0x10] << 24) | ((uint32_t)romData[0x11] << 16) |\n'
                     '               ((uint32_t)romData[0x12] << 8) | romData[0x13];\n    }')
        text = _once(text, 'class ZRom\n', 'extern std::vector<uint8_t>* gShipyardZapdRom;\n\nclass ZRom\n')
    elif name == 'ZAPD/ZRom.cpp':
        # The adapter hands over its normalized ROM buffer instead of a second disk read.
        text = _once(text, '\tromData = DiskFile::ReadAllBytes(romPath);',
                     '\tif (gShipyardZapdRom != nullptr)\n\t\tromData.swap(*gShipyardZapdRom);\n'
                     '\telse\n\t\tromData = DiskFile::ReadAllBytes(romPath);')
        text = _once(text, 'ZRom::ZRom(std::string romPath)', 'std::vector<uint8_t>* gShipyardZapdRom = nullptr;\n\n'
                     'ZRom::ZRom(std::string romPath)')
        # Files are copied out above; later reads use only the header's version word.
        text = _once(text, '\t\t//DiskFile::WriteAllBytes(StringHelper::Sprintf("baserom/%s", lines[i].c_str()), files[lines[i]]);\n'
                     '\t}\n}\n',
                     '\t\t//DiskFile::WriteAllBytes(StringHelper::Sprintf("baserom/%s", lines[i].c_str()), files[lines[i]]);\n'
                     '\t}\n\tstd::vector<uint8_t>(romData.begin(), romData.begin() + 0x40).swap(romData);\n}\n')
    elif name == 'ZAPD/ZPlayerAnimationData.cpp':
        # The raw size already spans every frame; upstream reserved frameCount times it.
        text = _once(text, '\tlimbRotData.reserve(totalSize * frameCount / 2);', '\tlimbRotData.reserve(totalSize / 2);')
    elif name == 'ZAPD/Utils/BitConverter.h':
        # Swap bytes explicitly; storing a big-endian read natively is a no-op on big-endian hosts.
        text = _once(text, '((uint16_t*)rom)[pos] = ToUInt16BE(rom, pos * 2);',
                     'uint8_t first = rom[pos * 2];\n\t\t\t\t\trom[pos * 2] = rom[pos * 2 + 1];\n'
                     '\t\t\t\t\trom[pos * 2 + 1] = first;')
        text = _once(text, '((uint32_t*)rom)[pos] = ToUInt32BE(rom, pos * 4);',
                     'uint8_t* word = rom + pos * 4;\n\t\t\t\t\tuint8_t first = word[0], second = word[1];\n'
                     '\t\t\t\t\tword[0] = word[3];\n\t\t\t\t\tword[1] = word[2];\n'
                     '\t\t\t\t\tword[2] = second;\n\t\t\t\t\tword[3] = first;')
        text = _once(text, '#if (defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)) || defined(__BIG_ENDIAN__)',
                     '#if (defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)) || defined(__BIG_ENDIAN__) || '
                     'defined(_M_PPCBE)')
    elif name == 'OTRExporter/Main.cpp':
        text = _otr_main(text)
    elif name == 'OTRExporter/Main.h':
        text = _once(text, '#include "ExporterArchive.h"\n\nextern std::shared_ptr<ExporterArchive> archive;\n'
                     'extern std::map<std::string, std::vector<char>> files;\n',
                     '#include <map>\n#include <string>\n#include <vector>\n')
        text = _once(text, 'void AddFile(std::string fName, std::vector<char> data);',
                     'void AddFile(std::string fName, std::vector<char> data);\n'
                     'bool ArchiveContains(const std::string& fName);')
    elif name == 'OTRExporter/DisplayListExporter.cpp':
        text = lower_assignments(text, parser, walk, {'value': ('Gfx', 6)})
        guard ='files.find(fName) == files.end() && !DiskFile::Exists("Extract/" + fName)'
        if text.count(guard) != 3:
            raise ValueError('Display-list export guard inventory changed')
        # Extract-directory mode never writes Extract/, so only the archive answers membership.
        text = text.replace(guard, '!ArchiveContains(fName)')
        # The dungeon pattern compiled once per call dominated path naming.
        text = _once(text, '\t\tstd::regex dungeonsWithMQ(', '\t\tstatic const std::regex dungeonsWithMQ(')
        text = _once(text, '\tstd::string prefix = GetPrefix(res);\n'
                     '\tstd::string fName = StringHelper::Sprintf("%s/%s", GetParentFolderName(res).c_str(), varName.c_str());',
                     '\tstd::string fName = StringHelper::Sprintf("%s/%s", GetParentFolderName(res).c_str(), varName.c_str());')
    elif name == 'ZAPD/ZFile.cpp':
        text = _type_timers(text)
        # Delegation to the default constructor becomes its statements at the top of each body.
        start = text.index('ZFile::ZFile()\n{\n') + len('ZFile::ZFile()\n{\n')
        defaults = text[start:text.index('}\n', start)]
        text = _once(text, ' : ZFile()\n{\n', '\n{\n' + defaults)
        text = _once(text, '\t: ZFile()\n{\n', '{\n' + defaults)
        # Archive extraction writes no files, so it creates no output folders.
        text = _once(text, '\tif (!Directory::Exists(outputPath))\n\t\tDirectory::CreateDirectory(outputPath.string());\n\n'
                     '\tif (!Directory::Exists(GetSourceOutputFolderPath()))\n'
                     '\t\tDirectory::CreateDirectory(GetSourceOutputFolderPath().string());\n\n', '')
    elif name == 'ZAPD/ZTexture.cpp':
        # Only the texture pool reads the hash, and archive extraction loads no pool.
        text = _once(text, '\t\tconst auto& parentRawData = parent->GetRawData();\n'
                     '\t\thash = CRC32B(parentRawData.data() + rawDataIndex, GetRawDataSize());\n',
                     '\t\tif (Globals::Instance->cfg.texturePool.empty())\n\t\t\treturn;\n'
                     '\t\tconst auto& parentRawData = parent->GetRawData();\n'
                     '\t\thash = CRC32B(parentRawData.data() + rawDataIndex, GetRawDataSize());\n')
    elif name == 'ZAPD/ZResource.cpp':
        text = _once(text, '\tResourceAttribute resAtrr;\n\tresAtrr.key = attr;\n\tresAtrr.isRequired = true;\n'
                     '\tregisteredAttributes[attr] = resAtrr;\n',
                     '\tregisteredAttributes.Register(attr, true, std::string());\n')
        text = _once(text, '\tResourceAttribute resAtrr;\n\tresAtrr.key = attr;\n\tresAtrr.value = defaultValue;\n'
                     '\tregisteredAttributes[attr] = resAtrr;\n',
                     '\tregisteredAttributes.Register(attr, false, defaultValue);\n')
    elif name == 'ZAPD/ZRoom/Commands/SetRoomList.cpp':
        text = _once(text, '\t: RoomEntry(BitConverter::ToInt32BE(rawData, rawDataIndex + 0),\n'
                     '                BitConverter::ToInt32BE(rawData, rawDataIndex + 4))\n{\n}',
                     '{\n\tvirtualAddressStart = BitConverter::ToInt32BE(rawData, rawDataIndex + 0);\n'
                     '\tvirtualAddressEnd = BitConverter::ToInt32BE(rawData, rawDataIndex + 4);\n}')
    elif name == 'ZAPD/ZRoom/Commands/SetMesh.cpp':
        # Single backgrounds never read unk_00 or id; desktop exports leave stack bytes there.
        text = _once(text, '\t: RoomShapeImageMultiBgEntry(nParent)\n{\n',
                     '\t: ZResource(nParent)\n{\n\tunk_00 = 0;\n\tid = 0;\n')
    elif name == 'ZAPD/ZAnimation.cpp':
        text = _once(text, '\t: CurveInterpKnot(parent, rawData, fileOffset + index * GetRawDataSize())\n{\n}',
                     '\t: parent(parent)\n{\n'
                     '\t*this = CurveInterpKnot(parent, rawData, fileOffset + index * GetRawDataSize());\n}')
        text = _once(text, 'transformDataArr.emplace_back(parent, rawData, transformDataOffset, i);',
                     'transformDataArr.push_back(CurveInterpKnot(parent, rawData, transformDataOffset, i));')
    elif name == 'OTRExporter/command_macros_base.h':
        text = _once(text, '#define CMD_F(a) {(a)}', '#define CMD_F(a) (a)')
    elif name == 'ZAPD/ImageBackend.cpp':
        # O2R export keeps bitmaps in memory only; PNG files are a desktop asset-build step.
        text = _once(text, '#include <png.h>\n', '#define PNG_COLOR_TYPE_RGB 2\n#define PNG_COLOR_TYPE_PALETTE 3\n'
                     '#define PNG_COLOR_TYPE_RGBA 6\n'
                     'typedef struct png_color_struct { uint8_t red, green, blue; } png_color;\n')
        for signature in ('void ImageBackend::ReadPng(const char* filename)\n',
                          'void ImageBackend::WritePng(const char* filename)\n'):
            following = 'const fs::path& filename)'
            start = text.index(signature)
            end = text.index(signature.split('(')[0] + '(' + following, start)
            text = (text[:start] + signature + '{\n\tHANDLE_ERROR(WarningType::InvalidPNG, "PNG files are unavailable", '
                    'filename);\n}\n\n' + text[end:])
    elif name == 'ZAPD/OutputFormatter.cpp':
        text = _once(text, ' nestIndent{indentation},\n', '\n')
        text = _once(text, 'spaceP{space}\n{\n', 'spaceP{space}\n{\n\tmemset(nestIndent, 0, sizeof(nestIndent));\n'
                     '\tnestIndent[0] = indentation;\n')
    elif name == 'OTRExporter/VersionInfo.cpp':
        start = text.index('\tresourceVersions = std::map<uint32_t, uint32_t> {\n')
        end = text.index('\t};\n', start) + len('\t};\n')
        entries = re.findall(r'\{ (static_cast<uint32_t>\([\w:]+\)), (\d+) \},', text[start:end])
        text = text[:start] + '\tresourceVersions.clear();\n' + ''.join(
            '\tresourceVersions.insert(std::make_pair(' + key + ', (uint32_t)' + value + '));\n'
            for key, value in entries) + text[end:]
    elif name == 'ZAPD/ZTextureAnimation.cpp':
        text = lower_assignments(text, parser, walk, {'currentPrimColor': ('F3DPrimColor', 1),
                                                       'currentEnvColor': ('F3DEnvColor', 1)})
    elif name == 'ZAPD/WarningHandler.cpp':
        text = lower_assignments(text, parser, walk, {'warningTypeToInfoMap[entry.second.type]': ('WarningInfo', 1),
                                                       'warningTypeToInfoMap[WarningType::Always]': ('WarningInfo', 1)})
        # Macro string concatenation defeats the parser in this function, so its loop is spelled here.
        start = text.index('void WarningHandler::PrintWarningsDebugInfo()')
        text = text[:start] + _once(
            text[start:], 'for (auto& it: warningTypeToInfoMap) {',
            'for (std::unordered_map<WarningType, WarningInfo>::iterator at = warningTypeToInfoMap.begin(); '
            'at != warningTypeToInfoMap.end(); ++at) {\n        std::pair<const WarningType, WarningInfo>& it = *at;')
    return text


def _zapd_main(text):
    text = _once(text, 'using ArgFunc = void (*)(int&, char**);', 'typedef void (*ArgFunc)(int&, char**);')
    text = _once(text, 'std::invoke(it->second, i, argv);', 'it->second(i, argv);')
    text = _once(text, '#include <atomic>\n', '')
    text = _once(text, '#include "CrashHandler.h"\n', '')
    text = _once(text, '#include <ctpl_stl.h>\n', '')
    text = _once(text, '\tCrashHandler_Init();\n', '')
    text = text.replace('std::atomic<size_t>*', 'volatile size_t*')
    text = _once(text, 'std::atomic<unsigned int> numWorkersLeft = 0;', 'unsigned int numWorkersLeft = 0;')
    text = _once(text, '\t\t\t\tconst int num_threads = std::thread::hardware_concurrency();\n'
                 '\t\t\t\tctpl::thread_pool pool(num_threads > 1 ? num_threads / 2 : 1);\n', '')
    # Upstream extraction is single-threaded; the pool branch never runs.
    text = _cut(text, '\t\t\t\t\telse\n\t\t\t\t\t{\n\t\t\t\t\t\tstd::string fileListItem = fileList[i];',
                '\t\t\t\t}\n\n\t\t\t\tif (!Globals::Instance->singleThreaded)')
    text = _cut(text, '\t\t\t\tif (!Globals::Instance->singleThreaded)\n\t\t\t\t{\n\t\t\t\t\twhile (true)',
                '\t\t\t\tauto end = std::chrono::steady_clock::now();')
    return _stage_timers(_shared_externals(text))


def _stage_timers(text):
    """Time each extraction stage so the adapter can report where a run spends its seconds."""
    text = _once(text, '\tParseArgs(argc, argv);\n',
                 '\t{\n\t\tShipyardZapdTimer timer(ZapdStageConfig);\n\t\tParseArgs(argc, argv);\n\t}\n')
    text = _once(text, '\t\tGlobals::Instance->rom = new ZRom(Globals::Instance->baseRomPath.string());\n',
                 '\t{\n\t\tShipyardZapdTimer timer(ZapdStageRom);\n'
                 '\t\tGlobals::Instance->rom = new ZRom(Globals::Instance->baseRomPath.string());\n\t}\n')
    load = '\ttinyxml2::XMLError eResult = doc.LoadFile(ShipyardZapdNative(xmlFilePath.string().c_str()).c_str());\n'
    text = _once(text, load, '\tShipyardZapdTimer xmlTimer(ZapdStageXml);\n' + load + '\txmlTimer.Stop();\n')
    parse = '\t\t\tZFile* file = new ZFile(fileMode, child, basePath, outPath, "", xmlFilePath, workerID);\n'
    text = _once(text, parse, '\t\t\tShipyardZapdTimer parseTimer(ZapdStageParse);\n' + parse + '\t\t\tparseTimer.Stop();\n')
    text = _once(text, '\t\t\t\tfile->ExtractResources();\n',
                 '\t\t\t{\n\t\t\t\tShipyardZapdTimer timer(ZapdStageExport);\n\t\t\t\tfile->ExtractResources();\n\t\t\t}\n')
    cleanup = '\tif (Globals::Instance->singleThreaded)\n\t{\n\t\tfor (int i = 0; i < Globals::Instance->files.size(); i++)'
    text = _once(text, cleanup, '\tShipyardZapdTimer freeTimer(ZapdStageFree);\n' + cleanup)
    return _once(text, '\tif (exporterSet != nullptr && exporterSet->endProgramFunc != nullptr)\n\t\texporterSet->endProgramFunc();\n',
                 '\tif (exporterSet != nullptr && exporterSet->endProgramFunc != nullptr)\n\t{\n'
                 '\t\tShipyardZapdTimer timer(ZapdStageFinish);\n\t\texporterSet->endProgramFunc();\n\t}\n')


def _type_timers(text):
    """Split the parse and export stages by resource type."""
    text = _once(text, '\t\t\t\t\tnRes->ExtractWithXML(child, rawDataIndex);\n',
                 '\t\t\t\t{\n\t\t\t\t\tShipyardZapdTypeTimer timer(ZapdStageParse, nodeName);\n'
                 '\t\t\t\t\tnRes->ExtractWithXML(child, rawDataIndex);\n\t\t\t\t}\n')
    text = _once(text, '\t\tDeclareResourceSubReferences();\n',
                 '\t{\n\t\tShipyardZapdTypeTimer timer(ZapdStageParse, "references");\n\t\tDeclareResourceSubReferences();\n\t}\n')
    late = ('\tfor (size_t i = 0; i < resources.size(); i++)\n\t\tresources[i]->ParseRawDataLate();\n'
            '\tfor (size_t i = 0; i < resources.size(); i++)\n\t\tresources[i]->DeclareReferencesLate(name);\n')
    text = _once(text, late, '\t{\n\tShipyardZapdTypeTimer timer(ZapdStageExport, "late");\n' + late + '\t}\n')
    return _once(text, '\t\tauto start = std::chrono::steady_clock::now();\n',
                 '\t\tauto start = std::chrono::steady_clock::now();\n'
                 '\t\tShipyardZapdTypeTimer typeTimer(ZapdStageExport, res->GetSourceTypeName());\n')



# Plain auto copies the parent's whole ROM file for each resource; these sites only read it.
_RAW_DATA_COPY = re.compile(r'\bauto (\w+) = (\w+(?:->\w+)*->GetRawData\(\));')


def _raw_data_views(files):
    total = 0
    for name in files:
        files[name], count = _RAW_DATA_COPY.subn(r'const auto& \1 = \2;', files[name])
        total += count
    if total != 5:
        raise ValueError('ZAPD raw data copy inventory changed: ' + str(total))
    return files

def _shared_externals(text):
    """Parse the config's external files once per run instead of once per listed file.

    Listed files only read external files' declarations, so every file sees the same
    external state; upstream rebuilds gameplay_keep and the others for each one.
    """
    text = _once(text, '#include <string>\n', '#include <string>\n#include <algorithm>\n')
    text = _once(text, 'int ExtractFunc(int workerID, int fileListSize, std::string fileListItem, ZFileMode fileMode)\n{\n',
                 'static std::vector<ZFile*> sConfigExternals;\n'
                 'static bool sConfigExternalsParsed = false;\n\n'
                 'int ExtractFunc(int workerID, int fileListSize, std::string fileListItem, ZFileMode fileMode)\n{\n')
    start = text.index('\tfor (auto& extFile : Globals::Instance->cfg.externalFiles)\n\t{\n')
    end = text.index('\tparseSuccessful = Parse(fileListItem, Globals::Instance->baseRomPath,')
    loop = text[start:end]
    text = text[:start] + (
        '\tif (sConfigExternalsParsed)\n'
        '\t{\n'
        '\t\tfor (size_t e = 0; e < sConfigExternals.size(); e++)\n'
        '\t\t{\n'
        '\t\t\tGlobals::Instance->AddFile(sConfigExternals[e], workerID);\n'
        '\t\t\tGlobals::Instance->AddExternalFile(sConfigExternals[e], workerID);\n'
        '\t\t\tGlobals::Instance->AddSegment(sConfigExternals[e]->segment, sConfigExternals[e], workerID);\n'
        '\t\t}\n'
        '\t}\n'
        '\telse\n'
        '\t{\n'
        '\tsize_t externalStart = Globals::Instance->files.size();\n'
        + loop.rstrip('\n') + '\n'
        '\tsConfigExternals.assign(Globals::Instance->files.begin() + externalStart, Globals::Instance->files.end());\n'
        '\tsConfigExternalsParsed = true;\n'
        '\t}\n\n') + text[end:]
    text = _once(text, '\t\tfor (int i = 0; i < Globals::Instance->files.size(); i++)\n\t\t{\n'
                 '\t\t\tdelete Globals::Instance->files[i];\n',
                 '\t\tfor (int i = 0; i < Globals::Instance->files.size(); i++)\n\t\t{\n'
                 '\t\t\tif (std::find(sConfigExternals.begin(), sConfigExternals.end(), Globals::Instance->files[i]) ==\n'
                 '\t\t\t    sConfigExternals.end())\n'
                 '\t\t\t\tdelete Globals::Instance->files[i];\n')
    text = _once(text, '\t\t\t\tauto end = std::chrono::steady_clock::now();',
                 '\t\t\t\tfor (size_t e = 0; e < sConfigExternals.size(); e++)\n'
                 '\t\t\t\t\tdelete sConfigExternals[e];\n'
                 '\t\t\t\tsConfigExternals.clear();\n'
                 '\t\t\t\tsConfigExternalsParsed = false;\n'
                 '\t\t\t\tauto end = std::chrono::steady_clock::now();')
    return text


def _otr_main(text):
    for include in ('#include <bit>\n', '#include <mutex>\n', '#include <ExporterArchiveO2R.h>\n',
                    '#include "ExporterArchiveOTR.h"\n'):
        text = _once(text, include, '')
    text = _once(text, '#include "Main.h"\n', '#include "Main.h"\n#include <set>\n#include <archive/stored_zip_writer.h>\n')
    text = _once(text, 'std::shared_ptr<ExporterArchive> archive;\n', '')
    text = _once(text, 'std::map<std::string, std::vector<char>> files;\nstd::mutex fileMutex;\n', '''
// Entries stream to the stored archive as they are exported. A non-MQ ROM renames
// the MQ logo; a vanilla logo of the same name still wins, as it sorts after it.
static Shipyard::Archive::StoredZipWriter sArchive;
static bool sArchiveOpen = false;
static std::map<std::string, std::vector<char> > sRenamedLogos;
static std::set<std::string> sGenuineLogos;

static void StreamFile(std::string fName, const std::vector<char>& data)
{
    ShipyardZapdTimer timer(ZapdStageArchive);
    if (!sArchiveOpen)
        sArchiveOpen = sArchive.Open(archiveFileName.c_str());
    if (fName.find("gTitleZeldaShieldLogoMQTex") != std::string::npos)
    {
        if (!Globals::Instance->rom->IsMQ())
        {
            fName.replace(fName.find("gTitleZeldaShieldLogoMQTex", 0), 27, "gTitleZeldaShieldLogoTex");
            sRenamedLogos[fName] = data;
            return;
        }
    }
    else if (fName.find("gTitleZeldaShieldLogoTex") != std::string::npos)
        sGenuineLogos.insert(fName);
    sArchive.Add(fName, data.empty() ? NULL : &data[0], data.size());
}

bool ArchiveContains(const std::string& fName)
{
    return sArchive.Contains(fName);
}
''')
    text = _cut(text, '        printf("BOTR: Generating OTR Archive...\\n");', '    }\n}\n')
    text = _once(text, '        std::string romPath = Globals::Instance->baseRomPath.string();\n'
                 '        std::vector<uint8_t> romData = DiskFile::ReadAllBytes(romPath);\n\n'
                 '        BitConverter::RomToBigEndian(romData.data(), romData.size());\n\n'
                 '        crc = BitConverter::ToUInt32BE(romData, 0x10);\n',
                 '        crc = Globals::Instance->rom->GetVersionCrc();\n')
    text = _cut(text, '        printf("Generating OTR Archive...\\n");', '    delete fileWriter;')
    text = _once(text, '    delete fileWriter;', '''        std::vector<char> versionBytes = versionStream->ToVector();
        std::vector<char> portVersionBytes = portVersionStream->ToVector();
        versionBytes.resize(versionStream->GetLength());
        portVersionBytes.resize(portVersionStream->GetLength());
        StreamFile("version", versionBytes);
        StreamFile("portVersion", portVersionBytes);
        for (std::map<std::string, std::vector<char> >::iterator it = sRenamedLogos.begin(); it != sRenamedLogos.end(); ++it)
        {
            if (!sGenuineLogos.count(it->first))
                sArchive.Add(it->first, it->second.empty() ? NULL : &it->second[0], it->second.size());
        }
        if (!sArchive.Finish())
            printf("Error: archive write failed\\n");
        sArchiveOpen = false;
        sRenamedLogos.clear();
        sGenuineLogos.clear();
    }

    delete fileWriter;''')
    # The custom asset archive is a desktop build step.
    text = _cut(text, '    files.clear();\n', '}\n\nstatic void ExporterParseArgs')
    text = _once(text, '            std::unique_lock Lock(fileMutex);\n            files[fName] = strem->ToVector();',
                 '            StreamFile(fName, strem->ToVector());')
    text = _once(text, '        std::unique_lock Lock(fileMutex);\n        files[fName] = data;', '        StreamFile(fName, data);')
    text = _once(text, '        portVersion = { 0, 0, 0 };', '        portVersion.assign(3, 0);')
    return text


# libultraship defines global utility classes of the same names; distinct names keep
# both copies, since their header-only methods would otherwise merge at link time.
_UTILITIES = re.compile(r'(?<![\w:.])(StringHelper|BitConverter|DiskFile|Directory|Path)(?=::)')
_UTILITY_CLASSES = re.compile(r'\bclass (StringHelper|BitConverter|DiskFile|Directory|Path)\b')


def _utility_names(text):
    return _UTILITY_CLASSES.sub(r'class Zapd\1', _UTILITIES.sub(r'Zapd\1', text))


def _syntax(text):
    text = scoped_enums(text)
    text = re.sub(r'\[\[[a-z_]+\]\]\s*', '', text)
    text = re.sub(r'\)\s*=\s*default;', ') {}', text)
    text = re.sub(r'\b(class \w+) final\b', r'\1', text)
    text = text.replace(' final override', ' override')
    text = re.sub(r'\bthread_local\s+', '', text)
    text = text.replace('#include <string_view>', '')
    text = text.replace('#include <chrono>', '#include <zapd/chrono.h>')
    text = text.replace('#include <cinttypes>', '#include <zapd/compat.h>')
    text = text.replace('#include <optional>', '#include <xbox_compat/optional.h>')
    text = re.sub(r'\bstd::(optional|nullopt)\b', r'xbox_compat::\1', text)
    text = re.sub(r'\bstd::string_view\b', 'std::string', text)
    text = text.replace(' noexcept', ' throw()')
    text = _FILESYSTEM.sub('#include <filesystem/filesystem.h>\nnamespace fs = xbox_compat::filesystem;\n', text)
    text = text.replace('#include <filesystem>', '#include <filesystem/filesystem.h>')
    text = re.sub(r'\bstd::filesystem\b', 'xbox_compat::filesystem', text)
    text = re.sub(r'R"\((.*?)\)"', lambda match: '"' + match[1].replace('\\', '\\\\').replace('"', '\\"') + '"',
                  text, flags=re.S)
    return text


def _member_initializers(text, parser):
    """Spell base and member initializers in C++03 form: A(x), never A::A(x) or a{x}."""
    data = text.encode()
    edits = []
    for node in walk(parser.parse(data).root_node):
        if node.type != 'field_initializer' or not node.named_children:
            continue
        target = node.named_children[0]
        parts = target.text.decode().split('::')
        if target.type == 'qualified_identifier' and len(parts) == 2 and parts[0] == parts[1]:
            edits.append((target.start_byte, target.end_byte, parts[0].encode()))
        value = node.named_children[-1]
        if value.type == 'initializer_list':
            if len(value.named_children) > 1:
                raise ValueError('Unsupported member brace list: ' + node.text.decode())
            edits.append((value.start_byte, value.start_byte + 1, b'('))
            edits.append((value.end_byte - 1, value.end_byte, b')'))
    for start, end, value in sorted(edits, reverse=True):
        data = data[:start] + value + data[end:]
    return data.decode()


def _classes_with_defaults(text, parser):
    for node in walk(parser.parse(text.encode()).root_node):
        if node.type not in ('class_specifier', 'struct_specifier'):
            continue
        body = node.child_by_field_name('body')
        title = node.child_by_field_name('name')
        if body is None or title is None:
            continue
        if any(_data_member(child) and child.child_by_field_name('default_value') is not None
               for child in body.named_children):
            yield title.text.decode()


def _data_member(node):
    return (node.type == 'field_declaration' and
            not any(part.type == 'storage_class_specifier' for part in node.named_children) and
            not any(part.type == 'function_declarator' for declarator in node.children_by_field_name('declarator')
                    for part in walk(declarator)))


def _member_defaults(headers, sources, parser):
    """Move in-class member defaults into every constructor of their class."""
    for name in sorted(headers):
        text = headers[name]
        for classname in list(_classes_with_defaults(text, parser)):
            text, fields = collect_member_defaults(text, classname, parser, walk)
            initializers = [(field.name, field.value) for field in fields]
            text, found = _inject_constructors(text, classname, initializers, parser, inline=True)
            for unit in sorted(sources):
                sources[unit], count = _inject_constructors(sources[unit], classname, initializers, parser)
                found += count
            if not found:
                text = _add_default_constructor(text, classname, initializers, parser)
        headers[name] = text


def _constructor_nodes(root, classname, inline):
    for node in walk(root):
        if node.type != 'function_definition':
            continue
        declarator = node.child_by_field_name('declarator')
        if declarator is None or declarator.type != 'function_declarator':
            continue
        target = declarator.child_by_field_name('declarator').text.decode().replace(' ', '')
        if inline:
            owner = node.parent.parent if node.parent is not None else None
            title = owner.child_by_field_name('name') if owner is not None else None
            if target == classname and title is not None and title.text.decode() == classname:
                yield node
        elif target == classname + '::' + classname:
            yield node


def _inject_constructors(text, classname, initializers, parser, inline=False):
    data = text.encode()
    edits = []
    for node in _constructor_nodes(parser.parse(data).root_node, classname, inline):
        existing = next((child for child in node.named_children if child.type == 'field_initializer_list'), None)
        named = set()
        if existing is not None:
            for child in existing.named_children:
                if child.type == 'field_initializer':
                    named.add(child.named_children[0].text.decode())
        added = ', '.join(member + '(' + value + ')' for member, value in initializers if member not in named)
        if not added:
            continue
        if existing is None:
            body = node.child_by_field_name('body')
            edits.append((body.start_byte, body.start_byte, (' : ' + added + ' ').encode()))
        else:
            edits.append((existing.end_byte, existing.end_byte, (', ' + added).encode()))
    for start, end, value in sorted(edits, reverse=True):
        data = data[:start] + value + data[end:]
    return data.decode(), len(edits)


def _add_default_constructor(text, classname, initializers, parser):
    # Value-initialize the other members too, as the aggregate forms this replaces did.
    data = text.encode()
    defaults = dict(initializers)
    for node in walk(parser.parse(data).root_node):
        title = node.child_by_field_name('name') if node.type in ('class_specifier', 'struct_specifier') else None
        if title is None or title.text.decode() != classname or node.child_by_field_name('body') is None:
            continue
        body = node.child_by_field_name('body')
        members = []
        for child in body.named_children:
            if not _data_member(child):
                continue
            for declarator in child.children_by_field_name('declarator'):
                if declarator.type == 'reference_declarator':
                    raise ValueError('Reference member without default: ' + classname)
                names = [part for part in walk(declarator) if part.type == 'field_identifier']
                members.append(names[0].text.decode())
        value = ('\n' + ('public: ' if node.type == 'class_specifier' else '') + classname + '() : ' +
                 ', '.join(member + '(' + defaults.get(member, '') + ')' for member in members) + ' {}\n')
        if node.type == 'class_specifier':
            # Restore the default access of the declarations that follow.
            value += 'private:\n'
        return (data[:body.start_byte + 1] + value.encode() + data[body.start_byte + 1:]).decode()
    raise ValueError('Missing class for member defaults: ' + classname)


def collect(zapd_root, otr_root):
    """Return the pinned sources as {projected path: text}."""
    files = {}
    for root, prefix in ((Path(zapd_root) / 'ZAPD', 'ZAPD/'), (Path(otr_root) / 'OTRExporter', 'OTRExporter/')):
        for path in sorted(root.rglob('*')):
            if path.suffix not in ('.h', '.cpp', '.c') or 'NuGet' in path.parts:
                continue
            files[prefix + path.relative_to(root).as_posix()] = path.read_bytes().decode().replace('\r\n', '\n')
    return files


GFXD_UNITS = ('gfxd.c', 'uc.c', 'uc_f3d.c', 'uc_f3db.c', 'uc_f3dex.c', 'uc_f3dex2.c', 'uc_f3dexb.c')


def gfxd_sources(gfxd_root):
    """Return ZAPDTR's libgfxd as {name: text} for the XDK; units then need gfxd.lower_preprocessed."""
    raw = {}
    for path in sorted(Path(gfxd_root).iterdir()):
        if path.suffix in ('.c', '.h'):
            raw[path.name] = path.read_bytes().decode().replace('\r\n', '\n')
    # Extraction runs gfxd on one thread, so its state needs no thread-local storage.
    raw['priv.h'] = _once(raw['priv.h'], '#define CONFIG_MT\n\n', '')
    return {name: gfxd.adapt(name, text, revision=gfxd.REVISION, gbi=raw['gbi.h']) for name, text in raw.items()}


def _enum_header(text, enum):
    """Keep one enum and its namespace from a consumer resource header."""
    namespace = re.search(r'^namespace (\w+) \{', text, re.M)
    body = re.search(r'^enum class ' + enum + r' \{[^}]*\};', text, re.M)
    if namespace is None or body is None:
        raise ValueError('Missing resource enum: ' + enum)
    return '#pragma once\nnamespace ' + namespace[1] + ' {\n' + body[0] + '\n}\n'


def project(zapd_root, otr_root, lus_root, resource_types, animation, output, parser):
    """Write the projected units; return their names and output hashes.

    resource_types and animation are the consumer headers declaring its resource
    identifiers and animation kinds.
    """
    output = Path(output)
    sources = collect(zapd_root, otr_root)
    for name, path in (('lus/ShipResourceType.h', Path(lus_root) / 'include/ship/resource/ResourceType.h'),
                       ('lus/FastResourceType.h', Path(lus_root) / 'include/fast/resource/ResourceType.h'),
                       ('lus/GameResourceType.h', Path(resource_types))):
        sources[name] = path.read_bytes().decode().replace('\r\n', '\n')
    sources['lus/GameAnimation.h'] = _enum_header(Path(animation).read_bytes().decode(), 'AnimationType')
    files = {name: _syntax(_utility_names(_file_specific(name, text, parser))) for name, text in sources.items()}
    files = _raw_data_views(files)
    headers = dict((name, text) for name, text in files.items() if name.endswith('.h'))
    sources = dict((name, text) for name, text in files.items() if not name.endswith('.h'))
    _member_defaults(headers, sources, parser)
    pending = {}
    for name, text in sorted(dict(headers, **sources).items()):
        if name.endswith(('.h', '.cpp')):
            text = lower_ranges(text.encode(), parser).decode()
            text = lower_table_initializers(text, parser, walk)
            text = _member_initializers(text, parser)
        pending[name] = lower_binary_integers(text, parser)
    units = sorted(name for name in pending if name.endswith('.cpp') and name not in _EXCLUDED)
    for name, text in pending.items():
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        data = text.encode()
        if not path.exists() or path.read_bytes() != data:
            path.write_bytes(data)
    return {'units': units,
            'outputs': {name: hashlib.sha256(text.encode()).hexdigest() for name, text in sorted(pending.items())}}
