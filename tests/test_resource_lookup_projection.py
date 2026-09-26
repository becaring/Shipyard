import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))
from xbox360_platform.resource_lookup import (path_hash, texture_hash_lookup,
    borrowed_cache_header, borrowed_cache_source, stateless_signature, texture_command_manager)

SOURCE = '''size_t ResourceIdentifier::CalculateHash() {
    size_t hash = Math::HashCombine(std::hash<std::string>()(Path), std::hash<uintptr_t>()(Owner));
    if (Parent != nullptr) {
        hash = Math::HashCombine(hash, std::hash<std::string>()(Parent->GetPath()));
    }
    return hash;
}
bool ResourceIdentifier::operator==(const ResourceIdentifier& rhs) const {
    return Owner == rhs.Owner && Path == rhs.Path && Parent == rhs.Parent;
}
'''


class ResourceLookupProjectionTests(unittest.TestCase):
    def test_texture_command_owns_manager_once(self):
        manager='Ship::Context::GetInstance()->GetResourceManager()'
        source=('bool gfx_set_timg_otr_hash_handler_custom(F3DGfx** cmd0) {\n'
                '    const char* fileName = '+manager+'->GetArchiveManager()->HashToCString(hash);\n'
                '    return '+manager+'->LoadResourceProcess(fileName);\n}\nbool next() {}')
        result=texture_command_manager(source)
        self.assertEqual(result.count(manager),1)
        self.assertIn('const auto resourceManager = '+manager,result)
        self.assertIn('return resourceManager->LoadResourceProcess(fileName);',result)
        with self.assertRaises(ValueError):texture_command_manager(result)

    def test_borrowed_header_requires_single_anchors(self):
        source=('    friend struct ResourceIdentifierHash;\n'
                '    bool operator==(const ResourceIdentifier& rhs) const;\n'
                '    size_t mHash;\n'
                '    std::shared_ptr<IResource> LoadResourceProcess(const std::string& filePath, bool loadExact = false,\n'
                'GetCachedResource(xbox_compat::variant<ResourceLoadError, std::shared_ptr<IResource>> cacheLine)')
        result=borrowed_cache_header(source)
        self.assertIn('ResourceIdentifier(const ResourceIdentifier& other);',result)
        self.assertIn('BorrowedLookup',result)
        with self.assertRaises(ValueError): borrowed_cache_header(source+source)
        with self.assertRaises(ValueError): borrowed_cache_header(source.replace('size_t mHash;', 'size_t mHashValue;'))

    def test_borrowed_source_preserves_authoritative_loader(self):
        constructor=('ResourceIdentifier::ResourceIdentifier(const std::string& path, uintptr_t owner, '
                     'std::shared_ptr<Archive> parent) : Path(path), Owner(owner), Parent(parent) {\n'
                     '    mHash = CalculateHash();\n}\n')
        loader=('std::shared_ptr<IResource> ResourceManager::LoadResourceProcess(const std::string& filePath, bool exact) {\n'
                '    return authoritative_loader(filePath, exact);\n}\n')
        cache=('ResourceManager::GetCachedResource(xbox_compat::variant<ResourceLoadError, std::shared_ptr<IResource>> cacheLine) {\n'
               '    auto resource = xbox_compat::get<std::shared_ptr<IResource>>(cacheLine);\n}\n')
        source=constructor+path_hash(SOURCE)+cache+loader
        result=borrowed_cache_source(source)
        self.assertTrue(result.endswith(loader))
        self.assertIn('Path(other.LookupData(), other.LookupSize())',result)
        self.assertIn('if (loadExact || !mAltAssetsEnabled)',result)
        with self.assertRaises(ValueError): borrowed_cache_source(source.replace('Path == rhs.Path','Path != rhs.Path'))

    def test_stateless_signature_keeps_pointer_guards(self):
        prefix='int check(const char* imgData) {\n    if (!imgData || ((uintptr_t)imgData & 1)) return 0;\n    '
        old='return Ship::Context::GetInstance()->GetResourceManager()->OtrSignatureCheck(imgData);'
        source=prefix+old+'\n}'
        result=stateless_signature(source)
        self.assertEqual(result,prefix+'return std::strncmp(imgData, "__OTR__", 7) == 0;\n}')
        with self.assertRaises(ValueError): stateless_signature(result)

    def test_only_hashes_change(self):
        result = path_hash(SOURCE)
        self.assertEqual(result.replace('#include "resources/path_hash.h"\n', '')
                         .replace('Shipyard::Resources::PathHash()', 'std::hash<std::string>()'), SOURCE)

    def test_changed_upstream_hash_is_rejected(self):
        with self.assertRaises(ValueError):
            path_hash(SOURCE.replace('std::hash<std::string>()(Path)', 'OtherHash(Path)'))

    def test_texture_reuses_validated_name(self):
        lookup = 'Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->HashToCString(hash)'
        source = ('bool gfx_set_timg_otr_hash_handler_custom(F3DGfx** cmd0) {\n'
                  '    const char* fileName = ' + lookup + ';\n'
                  '    if (fileName == nullptr) { (*cmd0)++; return false; }\n'
                  '    return LoadResourceProcess(' + lookup + ');\n}\n'
                  'bool next() { return true; }\n')
        result = texture_hash_lookup(source)
        self.assertEqual(result, source.replace('LoadResourceProcess(' + lookup + ')',
                                                'LoadResourceProcess(fileName)'))
        with self.assertRaises(ValueError):
            texture_hash_lookup(result)


if __name__ == '__main__':
    unittest.main()
