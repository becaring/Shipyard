#include <xtl.h>
#include "physical.h"

bool ShipyardInput_ReadPhysicalPad(unsigned slot, ShipyardInputRawPad* output) {
    XINPUT_STATE physicalState = {};
    if (slot >= 4 || !output || XInputGetState(slot, &physicalState) != ERROR_SUCCESS)
        return false;
    output->buttons = physicalState.Gamepad.wButtons;
    output->leftTrigger = physicalState.Gamepad.bLeftTrigger;
    output->rightTrigger = physicalState.Gamepad.bRightTrigger;
    output->leftX = physicalState.Gamepad.sThumbLX;
    output->leftY = physicalState.Gamepad.sThumbLY;
    output->rightX = physicalState.Gamepad.sThumbRX;
    output->rightY = physicalState.Gamepad.sThumbRY;
    return true;
}
bool ShipyardInput_PhysicalPadHasRumble(unsigned slot) {
    XINPUT_CAPABILITIES capabilities = {};
    return slot < 4 && XInputGetCapabilities(slot, 0, &capabilities) == ERROR_SUCCESS &&
           (capabilities.Vibration.wLeftMotorSpeed || capabilities.Vibration.wRightMotorSpeed);
}
bool ShipyardInput_WritePhysicalRumble(unsigned slot, uint16_t low, uint16_t high) {
    XINPUT_VIBRATION vibration = {};
    vibration.wLeftMotorSpeed = low;
    vibration.wRightMotorSpeed = high;
    return slot < 4 && XInputSetState(slot, &vibration) == ERROR_SUCCESS;
}
uint32_t ShipyardInput_InputMilliseconds() {
    return GetTickCount();
}
