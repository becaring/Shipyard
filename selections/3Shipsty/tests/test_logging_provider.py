"""The shared compiler projector exposes both consumer logging contracts."""

import unittest

from xbox360_platform import logging_core


class LoggingProvider(unittest.TestCase):
    def test_exact_anchor_rejects_missing_or_duplicate_source(self):
        with self.assertRaisesRegex(ValueError, "core match failed"):
            logging_core.exact("old old", "old", "new")
        with self.assertRaisesRegex(ValueError, "core match failed"):
            logging_core.exact("unrelated", "old", "new")

    def test_filtered_overloads_gate_conversion_before_packing(self):
        source = "FMT_VARIADIC(void,info,fmt::CStringRef)"
        output = logging_core.large_format_overloads(source, mode="filtered")
        self.assertIn("if(!should_log(level::info) && !should_backtrace()) return;", output)
        self.assertLess(output.index("should_log(level::info)"), output.index("MakeValue"))
        self.assertIn("ArgArray<39>", output)

    def test_filtered_overloads_preserve_fmt4_short_and_long_argument_paths(self):
        output = logging_core.large_format_overloads(
            "FMT_VARIADIC(void,info,fmt::CStringRef)", mode="filtered"
        )
        self.assertIn("ArgArray<15>::Type arr;\narr[0]=fmt::internal::MakeValue", output)
        self.assertIn("ArgArray<16>::Type arr;\narr[0]=fmt::internal::MakeArg", output)
        self.assertIn("arr[16].type=fmt::internal::Arg::NONE;", output)

    def test_free_functions_forward_to_default_logger(self):
        output = logging_core.large_format_overloads(
            "FMT_VARIADIC(void,info,fmt::CStringRef)", free_functions=True
        )
        self.assertIn("default_logger_raw()->info(p0,a0);", output)
        self.assertNotIn("should_log", output)

    def test_eager_overloads_keep_original_macro_for_short_arity(self):
        source = "FMT_VARIADIC(void,info,fmt::CStringRef)"
        output = logging_core.large_format_overloads(source, mode="eager")
        self.assertIn(source, output)
        self.assertIn("ArgArray<39>", output)
        self.assertNotIn("should_log", output)


if __name__ == "__main__":
    unittest.main()
