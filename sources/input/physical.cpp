#include "physical.h"
#include "../atomic/atomic.h"
#include <cstring>

struct ShipyardInputGamepad {
    ShipyardInputRawPad raw;
    uint16_t gameButtons;
    bool connected;
    bool hasRumble;
    bool rumbleTimed;
    unsigned slot;
    int32_t instance;
    uint32_t rumbleEndMilliseconds;
};
namespace {
ShipyardInputGamepad pads[4] = {};
// Buttons any pump saw held since the last game read, so a tap between game reads still counts.
Shipyard::Atomic::atomic<unsigned> heldSinceGameRead[4];
bool presentationSampleReady = false;
uint32_t generation = 1;
int32_t nextInstance = 1;
const char* lastError = "";
const char* names[] = {"Xbox 360 controller 1", "Xbox 360 controller 2", "Xbox 360 controller 3",
                       "Xbox 360 controller 4"};
// Indexed by ShipyardInputButton; zero marks controls absent from XInput.
const uint16_t buttonMasks[] = {
    0x1000, // A
    0x2000, // B
    0x4000, // X
    0x8000, // Y
    0x0020, // Back
    0,      // Guide
    0x0010, // Start
    0x0040, // Left stick
    0x0080, // Right stick
    0x0100, // Left shoulder
    0x0200, // Right shoulder
    0x0001, // Up
    0x0002, // Down
    0x0004, // Left
    0x0008, // Right
    0,      // Misc
    0,      // Paddle 1
    0,      // Paddle 2
    0,      // Paddle 3
    0,      // Paddle 4
    0       // Touchpad
};
bool valid(ShipyardInputGamepad* pad) {
    for (unsigned i = 0; i < 4; ++i)
        if (pad == &pads[i])
            return pad->connected;
    return false;
}
int16_t invert(int16_t value) {
    return value == -32768 ? 32767 : static_cast<int16_t>(-value);
}
int unsupported() {
    lastError = "XInput controller does not provide this capability";
    return -1;
}
} // namespace
void ShipyardInput_PumpGamepads() {
    presentationSampleReady = false;
    const uint32_t nowMilliseconds = ShipyardInput_InputMilliseconds();
    for (unsigned i = 0; i < 4; ++i) {
        ShipyardInputGamepad& pad = pads[i];
        pad.slot = i;
        ShipyardInputRawPad sampledState = {};
        bool connected = ShipyardInput_ReadPhysicalPad(i, &sampledState);
        if (connected != pad.connected) {
            ++generation;
            pad.connected = connected;
            pad.rumbleTimed = false;
            if (connected) {
                pad.instance = nextInstance++;
                pad.hasRumble = ShipyardInput_PhysicalPadHasRumble(i);
            } else {
                pad.hasRumble = false;
                ShipyardInput_WritePhysicalRumble(i, 0, 0);
            }
        }
        if (!connected)
            std::memset(&sampledState, 0, sizeof(sampledState));
        pad.raw = sampledState;
        unsigned held = heldSinceGameRead[i].load();
        while (sampledState.buttons && !heldSinceGameRead[i].compare_exchange_strong(held, held | sampledState.buttons)) {
        }
        if (pad.rumbleTimed &&
            static_cast<int32_t>(nowMilliseconds - pad.rumbleEndMilliseconds) >= 0) {
            ShipyardInput_WritePhysicalRumble(i, 0, 0);
            pad.rumbleTimed = false;
        }
    }
}
void ShipyardInput_PumpGamepadsForGame() {
    ShipyardInput_PumpGamepads();
    for (unsigned i = 0; i < 4; ++i)
        pads[i].gameButtons = static_cast<uint16_t>(heldSinceGameRead[i].exchange(0));
}
void ShipyardInput_BeginGamepadPresentation() {
    ShipyardInput_PumpGamepads();
    presentationSampleReady = true;
}
void ShipyardInput_RefreshGamepadsForGui() {
    // Consume the presentation sample once. An intervening pump invalidates it.
    if (!presentationSampleReady)
        ShipyardInput_PumpGamepads();
    presentationSampleReady = false;
}
bool ShipyardInput_ReadCachedGamepad(unsigned slot, ShipyardInputRawPad* state) {
    if (!state)
        return false;
    if (slot < 4 && pads[slot].connected) {
        *state = pads[slot].raw;
        return true;
    }
    std::memset(state, 0, sizeof(*state));
    return false;
}
uint32_t ShipyardInput_GamepadTopologyGeneration() {
    return generation;
}
int ShipyardInput_NumGamepads() {
    int count = 0;
    for (unsigned i = 0; i < 4; ++i)
        if (pads[i].connected)
            ++count;
    return count;
}
ShipyardInputGamepad* ShipyardInput_OpenGamepad(int index) {
    for (unsigned i = 0; i < 4; ++i)
        if (pads[i].connected && index-- == 0)
            return &pads[i];
    lastError = "No connected XInput controller at this device index";
    return 0;
}
ShipyardInputGuid ShipyardInput_GamepadGuid(int index) {
    ShipyardInputGuid guid = {};
    ShipyardInputGamepad* pad = ShipyardInput_OpenGamepad(index);
    if (pad) {
        guid.data[0] = 'X';
        guid.data[1] = 'I';
        guid.data[2] = 'N';
        guid.data[3] = 'P';
        guid.data[15] = static_cast<uint8_t>(pad->slot + 1);
    }
    return guid;
}
void ShipyardInput_GuidString(ShipyardInputGuid guid, char* text, int capacity) {
    if (!text || capacity <= 0)
        return;
    const char* hex = "0123456789abcdef";
    int n = capacity - 1;
    if (n > 32)
        n = 32;
    for (int i = 0; i < n; ++i)
        text[i] = hex[(guid.data[i / 2] >> (i % 2 ? 0 : 4)) & 15];
    text[n] = 0;
}
// XInput does not expose USB vendor identity; zero means unknown, not Microsoft.
uint16_t ShipyardInput_GamepadVendor(int) {
    return 0;
}
int ShipyardInput_IsGamepad(int index) {
    return ShipyardInput_OpenGamepad(index) != 0;
}
int32_t ShipyardInput_GamepadInstance(ShipyardInputGamepad* pad) {
    return valid(pad) ? pad->instance : -1;
}
ShipyardInputGamepad* ShipyardInput_GamepadJoystick(ShipyardInputGamepad* pad) {
    return valid(pad) ? pad : 0;
}
const char* ShipyardInput_GamepadName(ShipyardInputGamepad* pad) {
    return valid(pad) ? names[pad->slot] : 0;
}
const char* ShipyardInput_GamepadError() {
    return lastError;
}
uint8_t ShipyardInput_GamepadButton(ShipyardInputGamepad* pad, ShipyardInputButton button) {
    if (!valid(pad) || button < 0 || button >= ShipyardButtonMax)
        return 0;
    return ((pad->raw.buttons | pad->gameButtons) & buttonMasks[button]) ? 1 : 0;
}
int16_t ShipyardInput_GamepadAxis(ShipyardInputGamepad* pad, ShipyardInputAxis axis) {
    if (!valid(pad))
        return 0;
    switch (axis) {
    case ShipyardAxisLeftX:
        return pad->raw.leftX;
    case ShipyardAxisLeftY:
        return invert(pad->raw.leftY);
    case ShipyardAxisRightX:
        return pad->raw.rightX;
    case ShipyardAxisRightY:
        return invert(pad->raw.rightY);
    case ShipyardAxisLeftTrigger:
        return static_cast<int16_t>(pad->raw.leftTrigger * 32767 / 255);
    case ShipyardAxisRightTrigger:
        return static_cast<int16_t>(pad->raw.rightTrigger * 32767 / 255);
    default:
        return 0;
    }
}
int ShipyardInput_GamepadHasRumble(ShipyardInputGamepad* pad) {
    return valid(pad) && pad->hasRumble;
}
int ShipyardInput_GamepadRumble(ShipyardInputGamepad* pad, uint16_t low, uint16_t high,
                                uint32_t milliseconds) {
    if (!valid(pad) || !pad->hasRumble)
        return unsupported();
    if (!ShipyardInput_WritePhysicalRumble(pad->slot, low, high)) {
        lastError = "XInputSetState failed";
        return -1;
    }
    // Zero duration requests sustained rumble; zero motors explicitly stop it.
    pad->rumbleTimed = milliseconds != 0 && (low != 0 || high != 0);
    if (milliseconds > 0x7fffffff)
        milliseconds = 0x7fffffff;
    pad->rumbleEndMilliseconds = ShipyardInput_InputMilliseconds() + milliseconds;
    return 0;
}
void ShipyardInput_StopPhysicalRumble() {
    for (unsigned i = 0; i < 4; ++i) {
        ShipyardInput_WritePhysicalRumble(i, 0, 0);
        pads[i].rumbleTimed = false;
    }
}
int ShipyardInput_GamepadHasSensor(ShipyardInputGamepad*, int) {
    return 0;
}
int ShipyardInput_GamepadEnableSensor(ShipyardInputGamepad*, int, int) {
    return unsupported();
}
int ShipyardInput_GamepadSensorData(ShipyardInputGamepad*, int, float*, int) {
    return unsupported();
}
int ShipyardInput_GamepadHasLED(ShipyardInputGamepad*) {
    return 0;
}
int ShipyardInput_GamepadLED(ShipyardInputGamepad*, uint8_t, uint8_t, uint8_t) {
    return unsupported();
}
