"""The shared projector splits only the combiner lookup's warm/cold boundary."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))
from xbox360_platform import combiner_lookup


SOURCE = '''ColorCombiner* Interpreter::LookupOrCreateColorCombiner(const ColorCombinerKey& key) {
    if (mPrevCombiner != mColorCombinerPool.end() && mPrevCombiner->first == key) {
        return &mPrevCombiner->second;
    }
    mPrevCombiner = mColorCombinerPool.find(key);
    if (mPrevCombiner != mColorCombinerPool.end()) {
        return &mPrevCombiner->second;
    }
    Flush();
    mPrevCombiner = mColorCombinerPool.insert(std::make_pair(key, ColorCombiner())).first;
    GenerateCC(&mPrevCombiner->second, key);
    return &mPrevCombiner->second;
}
'''


class CombinerLookupProjectionTests(unittest.TestCase):
    def test_split_preserves_warm_and_cold_behavior(self):
        result = combiner_lookup.transform(SOURCE)
        self.assertIn('__forceinline ColorCombiner* Interpreter::LookupOrCreateColorCombiner', result)
        self.assertIn('__declspec(noinline) ColorCombiner* Interpreter::XboxLookupColorCombinerCold', result)
        self.assertIn('    return XboxLookupColorCombinerCold(key);\n}', result)
        self.assertEqual(result.count('mColorCombinerPool.find(key)'), 1)
        self.assertEqual(result.count('Flush();'), 1)
        self.assertEqual(result.count('GenerateCC(&mPrevCombiner->second, key);'), 1)

    def test_header_adds_one_cold_declaration(self):
        source = '    ColorCombiner* LookupOrCreateColorCombiner(const ColorCombinerKey& key);'
        result = combiner_lookup.header(source)
        self.assertEqual(result.count('XboxLookupColorCombinerCold'), 1)

    def test_changed_lookup_body_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'combiner lookup'):
            combiner_lookup.transform(SOURCE.replace('mColorCombinerPool.find(key)',
                                                     'mColorCombinerPool.lower_bound(key)'))


if __name__ == '__main__':
    unittest.main()
