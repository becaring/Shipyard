"""libultraship controller patches shared by every consumer.

Run both on raw upstream text, before structured-binding lowering. Either order
with overlap_lus.control_deck works: the snapshot path calls the deck's own
GamepadGameInputBlocked on render, so it inherits the modal block.
"""


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError("Controller projection anchor drift: " + old[:80])
    return text.replace(old, new)


def gamepad_modal(text):
    """The keyboard is blocked behind an ImGui modal; block the gamepad too,
    so A activates only the selected popup button."""
    signature = "bool ControlDeck::GamepadGameInputBlocked() {"
    start = text.index(signature)
    end = text.index("\n}\n", start)
    body = once(text[start:end], "    return AllGameInputBlocked() ||\n",
                "    return AllGameInputBlocked() || ImGui::GetTopMostPopupModal() != NULL ||\n")
    return text[:start] + body + text[end:]


STICK = "const std::pair<std::string, std::shared_ptr<ControllerAxisDirectionMapping>> "


def borrow_polling(name, text):
    """Borrow controller map entries during polling without changing call order.

    Mapping callbacks do not edit their owning maps. The comparator takes the
    map's const-key value_type, so binding a reference cannot build a converting
    pair (a string and shared_ptr copy per compared mapping).
    """
    name = name.replace("\\", "/")
    if name.endswith("/libultraship/controller/controldevice/controller/Controller.cpp"):
        return once(text, "for (auto [bitmask, button] : mButtons)",
                    "for (const auto& [bitmask, button] : mButtons)")
    if name.endswith("/ship/controller/controldevice/controller/ControllerStick.cpp"):
        # The XDK compiler deduces no return type for this comparator.
        for argument, tail in (("a", ""), ("b)", " -> bool")):
            text = once(text, STICK + argument,
                        "const std::unordered_map<std::string, std::shared_ptr<ControllerAxisDirectionMapping> >"
                        "::value_type& " + argument + tail)
    return text
