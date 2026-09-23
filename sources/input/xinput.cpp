#include <xtl.h>
#include "physical.h"

bool ShipyardInput_ReadPhysicalPad(unsigned slot,ShipyardInputRawPad* out){
    XINPUT_STATE state={};
    if(slot>=4||!out||XInputGetState(slot,&state)!=ERROR_SUCCESS)return false;
    out->buttons=state.Gamepad.wButtons;out->leftTrigger=state.Gamepad.bLeftTrigger;
    out->rightTrigger=state.Gamepad.bRightTrigger;out->leftX=state.Gamepad.sThumbLX;
    out->leftY=state.Gamepad.sThumbLY;out->rightX=state.Gamepad.sThumbRX;out->rightY=state.Gamepad.sThumbRY;
    return true;
}
bool ShipyardInput_PhysicalPadHasRumble(unsigned slot){
    XINPUT_CAPABILITIES caps={};return slot<4&&XInputGetCapabilities(slot,0,&caps)==ERROR_SUCCESS&&
        (caps.Vibration.wLeftMotorSpeed||caps.Vibration.wRightMotorSpeed);
}
bool ShipyardInput_WritePhysicalRumble(unsigned slot,uint16_t low,uint16_t high){
    XINPUT_VIBRATION vibration={};vibration.wLeftMotorSpeed=low;vibration.wRightMotorSpeed=high;
    return slot<4&&XInputSetState(slot,&vibration)==ERROR_SUCCESS;
}
uint32_t ShipyardInput_InputMilliseconds(){return GetTickCount();}
