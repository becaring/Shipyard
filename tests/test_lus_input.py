import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from xbox360_platform import lus_input, overlap_lus

DECK = """bool ControlDeck::GamepadGameInputBlocked() {
    // block controller input when using the controller to navigate imgui menus
    return AllGameInputBlocked() ||
           Context::GetInstance()->GetWindow()->GetGui()->GetMenuOrMenubarVisible() &&
               Ship::Context::GetInstance()->GetConsoleVariables()->GetInteger(CVAR_IMGUI_CONTROLLER_NAV, 0);
}

bool ControlDeck::KeyboardGameInputBlocked() {
    return AllGameInputBlocked() ||
           ImGui::GetTopMostPopupModal() != NULL;
}

bool ControlDeck::MouseGameInputBlocked() {
    return AllGameInputBlocked();
}
"""


class LusInputTests(unittest.TestCase):
    def test_gamepad_blocks_behind_modal_with_snapshot_in_either_order(self):
        for text in (overlap_lus.control_deck(lus_input.gamepad_modal(DECK)),
                     lus_input.gamepad_modal(overlap_lus.control_deck(DECK))):
            gamepad = text[text.index("GamepadGameInputBlocked() {"):text.index("KeyboardGameInputBlocked() {")]
            self.assertLess(gamepad.index("Shipyard_OverlapInputBlocked(0"), gamepad.index("GetTopMostPopupModal"))
            self.assertIn("return AllGameInputBlocked() || ImGui::GetTopMostPopupModal() != NULL ||\n"
                          "           Context::", gamepad)
            keyboard = text[text.index("KeyboardGameInputBlocked() {"):]
            self.assertEqual(keyboard.count("GetTopMostPopupModal"), 1)
        with self.assertRaises(ValueError):
            lus_input.gamepad_modal(DECK.replace("AllGameInputBlocked() ||\n           Context", "Context"))

    def test_polling_borrows_map_entries(self):
        controller = "void Controller::ReadToOSContPad(OSContPad* pad) {\n    for (auto [bitmask, button] : mButtons) {\n"
        text = lus_input.borrow_polling("libultraship/src/libultraship/controller/controldevice/controller/Controller.cpp",
                                        controller)
        self.assertIn("for (const auto& [bitmask, button] : mButtons)", text)
        pair = "const std::pair<std::string, std::shared_ptr<ControllerAxisDirectionMapping>> "
        stick = "[](" + pair + "a,\n   " + pair + "b) {"
        text = lus_input.borrow_polling("src/ship/controller/controldevice/controller/ControllerStick.cpp", stick)
        self.assertEqual(text.count("ControllerAxisDirectionMapping> >::value_type& "), 2)
        self.assertNotIn("std::pair", text)
        self.assertEqual(lus_input.borrow_polling("src/ship/Other.cpp", controller), controller)
        with self.assertRaises(ValueError):
            lus_input.borrow_polling("src/libultraship/controller/controldevice/controller/Controller.cpp", "")


if __name__ == "__main__":
    unittest.main()
