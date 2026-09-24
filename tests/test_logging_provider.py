"""The shared compiler projector exposes both consumer logging contracts."""

import unittest

from xbox360_platform import logging_core


class LoggingProvider(unittest.TestCase):
    def test_filtered_overloads_gate_conversion_before_packing(self):
        source = "FMT_VARIADIC(void,info,fmt::CStringRef)"
        output = logging_core.large_format_overloads(source, mode="filtered")
        self.assertIn("if(!should_log(level::info) && !should_backtrace()) return;", output)
        self.assertLess(output.index("should_log(level::info)"), output.index("MakeValue"))
        self.assertIn("ArgArray<39>", output)

    def test_eager_overloads_keep_original_macro_for_short_arity(self):
        source = "FMT_VARIADIC(void,info,fmt::CStringRef)"
        output = logging_core.large_format_overloads(source, mode="eager")
        self.assertIn(source, output)
        self.assertIn("ArgArray<39>", output)
        self.assertNotIn("should_log", output)


if __name__ == "__main__":
    unittest.main()
