#pragma once
#include <stdint.h>

// Physical operations only. Mapping, deadzones, routing and configuration stay
// in the pinned current Ship classes. No application-level input injection.
struct ShipyardInputRawPad {
    uint16_t buttons;
    uint8_t leftTrigger, rightTrigger;
    int16_t leftX, leftY, rightX, rightY;
};
bool ShipyardInput_ReadPhysicalPad(unsigned slot, ShipyardInputRawPad* state);
bool ShipyardInput_PhysicalPadHasRumble(unsigned slot);
bool ShipyardInput_WritePhysicalRumble(unsigned slot, uint16_t low, uint16_t high);
uint32_t ShipyardInput_InputMilliseconds();
void ShipyardInput_PumpGamepads();
// Sample every physical slot for this presentation. The GUI device handler may
// consume it once; ordinary window/game pumps always sample again.
void ShipyardInput_BeginGamepadPresentation();
void ShipyardInput_RefreshGamepadsForGui();
// Read the latest pumped raw physical slot without polling. Disconnected or
// invalid slots return false and clear state; slot is not a compact device index.
bool ShipyardInput_ReadCachedGamepad(unsigned slot, ShipyardInputRawPad* state);
uint32_t ShipyardInput_GamepadTopologyGeneration();
void ShipyardInput_StopPhysicalRumble();

struct ShipyardInputGamepad;
struct ShipyardInputGuid { uint8_t data[16]; };
enum ShipyardInputButton { ShipyardButtonInvalid=-1, ShipyardButtonA, ShipyardButtonB, ShipyardButtonX, ShipyardButtonY,
    ShipyardButtonBack, ShipyardButtonGuide, ShipyardButtonStart, ShipyardButtonLeftStick, ShipyardButtonRightStick,
    ShipyardButtonLeftShoulder, ShipyardButtonRightShoulder, ShipyardButtonUp, ShipyardButtonDown, ShipyardButtonLeft,
    ShipyardButtonRight, ShipyardButtonMisc, ShipyardButtonPaddle1, ShipyardButtonPaddle2, ShipyardButtonPaddle3,
    ShipyardButtonPaddle4, ShipyardButtonTouchpad, ShipyardButtonMax };
enum ShipyardInputAxis { ShipyardAxisInvalid=-1, ShipyardAxisLeftX, ShipyardAxisLeftY, ShipyardAxisRightX,
    ShipyardAxisRightY, ShipyardAxisLeftTrigger, ShipyardAxisRightTrigger, ShipyardAxisMax };
int ShipyardInput_NumGamepads();
ShipyardInputGamepad* ShipyardInput_OpenGamepad(int index);
ShipyardInputGuid ShipyardInput_GamepadGuid(int index);
void ShipyardInput_GuidString(ShipyardInputGuid guid,char* text,int capacity);
uint16_t ShipyardInput_GamepadVendor(int index);
int ShipyardInput_IsGamepad(int index);
int32_t ShipyardInput_GamepadInstance(ShipyardInputGamepad* pad);
ShipyardInputGamepad* ShipyardInput_GamepadJoystick(ShipyardInputGamepad* pad);
const char* ShipyardInput_GamepadName(ShipyardInputGamepad* pad);
const char* ShipyardInput_GamepadError();
uint8_t ShipyardInput_GamepadButton(ShipyardInputGamepad* pad,ShipyardInputButton button);
int16_t ShipyardInput_GamepadAxis(ShipyardInputGamepad* pad,ShipyardInputAxis axis);
int ShipyardInput_GamepadHasRumble(ShipyardInputGamepad* pad);
int ShipyardInput_GamepadRumble(ShipyardInputGamepad* pad,uint16_t low,uint16_t high,uint32_t milliseconds);
int ShipyardInput_GamepadHasSensor(ShipyardInputGamepad*,int);
int ShipyardInput_GamepadEnableSensor(ShipyardInputGamepad*,int,int);
int ShipyardInput_GamepadSensorData(ShipyardInputGamepad*,int,float*,int);
int ShipyardInput_GamepadHasLED(ShipyardInputGamepad*);
int ShipyardInput_GamepadLED(ShipyardInputGamepad*,uint8_t,uint8_t,uint8_t);
