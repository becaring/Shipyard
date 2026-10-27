"""Game-side audio barrier inventory and entry insertion."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from xbox360_platform import overlap_audio

LIBRARY = """\
s32 AudioThread_NextRandom(void) { return 4; }
void AudioScript_SequencePlayerDisable(SequencePlayer* player) { player->enabled = 0; }
"""
GAME = """\
static u8 sQueue[4];
void Audio_QueueOnly(u8 id) {
    sQueue[0] = id;
}
s32 Audio_Random(void) {
    return AudioThread_NextRandom();
}
void Audio_ReadsState(void) {
    s32 count = gAudioCtx.numNotes;
    (void)count;
}
void Audio_Command(void) {
    AUDIOCMD_GLOBAL_STOP_AUDIOCMDS();
}
void Audio_Disable(void) {
    AudioScript_SequencePlayerDisable(0);
}
void Audio_Calls(void) {
    Audio_Disable();
}
"""


class OverlapAudioTest(unittest.TestCase):
    def test_inventory_is_direct_touchers_only(self):
        names = overlap_audio.touching({"game.c": GAME}, {"lib.c": LIBRARY},
                                       benign=("AudioThread_NextRandom",))
        self.assertEqual(names, {"Audio_ReadsState", "Audio_Command", "Audio_Disable"})
        names = overlap_audio.touching({"game.c": GAME}, {"lib.c": LIBRARY})
        self.assertIn("Audio_Random", names)

    def test_barrier_runs_before_declarations_and_body(self):
        text, inserted = overlap_audio.barriers(GAME, {"Audio_ReadsState", "Audio_Command", "Missing"})
        self.assertEqual(inserted, {"Audio_ReadsState", "Audio_Command"})
        self.assertIn("void Audio_ReadsState(void) {\n    Shipyard_OverlapAudioBarrier();\n"
                      "    s32 count = gAudioCtx.numNotes;", text)
        self.assertIn("void Audio_Command(void) {\n    Shipyard_OverlapAudioBarrier();\n"
                      "    AUDIOCMD_GLOBAL", text)
        self.assertEqual(text.count(overlap_audio.BARRIER), 2)

    def test_duplicate_definitions_fail_closed(self):
        with self.assertRaises(ValueError):
            overlap_audio.barriers("void A(void) {}\nvoid A(void) {}\n", {"A"})


if __name__ == "__main__":
    unittest.main()
