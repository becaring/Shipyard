import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))
from xbox360_platform.interpolation_replay import prune_discarded_stack, recycle_maps

PREPARED = '''struct InterpolateCtx {
    void interpolate_branch(Path* old_path, Path* new_path) {
        if (!prepared_interpolation_valid) {
            prepare_branch(old_path, new_path);
            prepared_interpolation_valid = true;
        }
        std::vector<MtxF*> slots;
        for (size_t i = 0; i < prepared_destinations.size(); ++i)
            slots.push_back(&mtx_replacements[prepared_destinations[i]]);
    }
};
unordered_map<Mtx*, MtxF> FrameInterpolation_Interpolate(float step) {
'''


class InterpolationReplayTests(unittest.TestCase):
    def test_prune_uses_consumer_enum_access(self):
        strong = prune_discarded_stack(PREPARED, value_expression='{value}.GetValue()')
        plain = prune_discarded_stack(PREPARED, value_expression='{value}')
        self.assertIn('switch (op.GetValue())', strong)
        self.assertIn('switch (op)', plain)
        for result in (strong, plain):
            self.assertLess(result.index('PruneDiscardedInterpolationStack() {'), result.index('struct InterpolateCtx {'))
            self.assertIn('prepare_branch(old_path, new_path);\n            PruneDiscardedInterpolationStack();\n'
                          '            prepared_interpolation_valid = true;', result)
        with self.assertRaises(ValueError):
            prune_discarded_stack(strong, value_expression='{value}')

    def test_recycle_keeps_fallback_and_exports_return(self):
        result = recycle_maps(PREPARED)
        self.assertTrue(result.startswith('#ifdef _XBOX\n#include <xtl.h>\n'))
        self.assertIn('static RecycledInterpolationMaps recycled_interpolation_maps;', result)
        self.assertIn('if (!recycled_interpolation_maps.Acquire(mtx_replacements, prepared_destinations, slots)) {', result)
        self.assertIn('                slots.push_back(&mtx_replacements[prepared_destinations[i]]);', result)
        self.assertIn('void FrameInterpolation_Recycle(std::unordered_map<Mtx*, MtxF>& replacements) {', result)
        with self.assertRaises(ValueError):
            recycle_maps(result)


if __name__ == '__main__':
    unittest.main()
