import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))
from xbox360_platform.resource_lookup import path_hash, texture_hash_lookup

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
