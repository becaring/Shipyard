#include "physical.h"
#include <cstring>

struct ShipyardInputGamepad {
    ShipyardInputRawPad raw;
    bool connected, rumble, timed;
    unsigned slot;
    int32_t instance;
    uint32_t endTime;
};
namespace {
ShipyardInputGamepad pads[4]={};
bool presentationSampleReady=false;
uint32_t generation=1;
int32_t nextInstance=1;
const char* lastError="";
const char* names[]={"Xbox 360 controller 1","Xbox 360 controller 2","Xbox 360 controller 3","Xbox 360 controller 4"};
const uint16_t buttonMasks[]={0x1000,0x2000,0x4000,0x8000,0x0020,0,0x0010,0x0040,0x0080,
    0x0100,0x0200,0x0001,0x0002,0x0004,0x0008,0,0,0,0,0,0};
bool valid(ShipyardInputGamepad* p) {
    for(unsigned i=0;i<4;++i) if(p==&pads[i]) return p->connected;
    return false;
}
int16_t invert(int16_t value) { return value==-32768?32767:static_cast<int16_t>(-value); }
int unsupported(){lastError="XInput controller does not provide this capability";return -1;}
}
void ShipyardInput_PumpGamepads(){
    presentationSampleReady=false;
    const uint32_t now=ShipyardInput_InputMilliseconds();
    for(unsigned i=0;i<4;++i){
        ShipyardInputGamepad& pad=pads[i];pad.slot=i;
        ShipyardInputRawPad next={};
        bool connected=ShipyardInput_ReadPhysicalPad(i,&next);
        if(connected!=pad.connected){
            ++generation;
            pad.connected=connected;pad.timed=false;
            if(connected){pad.instance=nextInstance++;pad.rumble=ShipyardInput_PhysicalPadHasRumble(i);}
            else {pad.rumble=false;ShipyardInput_WritePhysicalRumble(i,0,0);}
        }
        if(!connected) std::memset(&next,0,sizeof(next));
        pad.raw=next;
        if(pad.timed && static_cast<int32_t>(now-pad.endTime)>=0){
            ShipyardInput_WritePhysicalRumble(i,0,0);pad.timed=false;
        }
    }
}
void ShipyardInput_BeginGamepadPresentation(){
    ShipyardInput_PumpGamepads();
    presentationSampleReady=true;
}
void ShipyardInput_RefreshGamepadsForGui(){
    // Gui::StartFrame samples before DrawMenu updates the device handler. If
    // that order is bypassed, or another pump intervenes, obtain a fresh sample.
    if(!presentationSampleReady) ShipyardInput_PumpGamepads();
    presentationSampleReady=false;
}
bool ShipyardInput_ReadCachedGamepad(unsigned slot,ShipyardInputRawPad* state){
    if(!state)return false;
    if(slot<4&&pads[slot].connected){*state=pads[slot].raw;return true;}
    std::memset(state,0,sizeof(*state));return false;
}
uint32_t ShipyardInput_GamepadTopologyGeneration(){return generation;}
int ShipyardInput_NumGamepads(){int n=0;for(unsigned i=0;i<4;++i)if(pads[i].connected)++n;return n;}
ShipyardInputGamepad* ShipyardInput_OpenGamepad(int index){
    for(unsigned i=0;i<4;++i)if(pads[i].connected && index--==0)return &pads[i];
    lastError="No connected XInput controller at this device index";return 0;
}
ShipyardInputGuid ShipyardInput_GamepadGuid(int index){
    ShipyardInputGuid guid={};ShipyardInputGamepad* p=ShipyardInput_OpenGamepad(index);
    if(p){guid.data[0]='X';guid.data[1]='I';guid.data[2]='N';guid.data[3]='P';guid.data[15]=static_cast<uint8_t>(p->slot+1);}
    return guid;
}
void ShipyardInput_GuidString(ShipyardInputGuid guid,char* text,int capacity){
    if(!text || capacity<=0)return;
    const char* hex="0123456789abcdef";int n=capacity-1;if(n>32)n=32;
    for(int i=0;i<n;++i)text[i]=hex[(guid.data[i/2]>>(i%2?0:4))&15];text[n]=0;
}
// XInput does not expose USB vendor identity; zero means unknown, not Microsoft.
uint16_t ShipyardInput_GamepadVendor(int){return 0;}
int ShipyardInput_IsGamepad(int index){return ShipyardInput_OpenGamepad(index)!=0;}
int32_t ShipyardInput_GamepadInstance(ShipyardInputGamepad* p){return valid(p)?p->instance:-1;}
ShipyardInputGamepad* ShipyardInput_GamepadJoystick(ShipyardInputGamepad* p){return valid(p)?p:0;}
const char* ShipyardInput_GamepadName(ShipyardInputGamepad* p){return valid(p)?names[p->slot]:0;}
const char* ShipyardInput_GamepadError(){return lastError;}
uint8_t ShipyardInput_GamepadButton(ShipyardInputGamepad* p,ShipyardInputButton b){return valid(p)&&b>=0&&b<ShipyardButtonMax&&(p->raw.buttons&buttonMasks[b])?1:0;}
int16_t ShipyardInput_GamepadAxis(ShipyardInputGamepad* p,ShipyardInputAxis axis){
    if(!valid(p))return 0;
    switch(axis){
    case ShipyardAxisLeftX:return p->raw.leftX;case ShipyardAxisLeftY:return invert(p->raw.leftY);
    case ShipyardAxisRightX:return p->raw.rightX;case ShipyardAxisRightY:return invert(p->raw.rightY);
    case ShipyardAxisLeftTrigger:return static_cast<int16_t>(p->raw.leftTrigger*32767/255);
    case ShipyardAxisRightTrigger:return static_cast<int16_t>(p->raw.rightTrigger*32767/255);
    default:return 0;}
}
int ShipyardInput_GamepadHasRumble(ShipyardInputGamepad* p){return valid(p)&&p->rumble;}
int ShipyardInput_GamepadRumble(ShipyardInputGamepad* p,uint16_t low,uint16_t high,uint32_t ms){
    if(!valid(p)||!p->rumble)return unsupported();
    if(!ShipyardInput_WritePhysicalRumble(p->slot,low,high)){lastError="XInputSetState failed";return -1;}
    // Current SDLRumbleMapping passes duration zero for a sustained effect and
    // explicitly stops with zero motors. Preserve that application contract.
    p->timed=ms!=0&&(low!=0||high!=0);if(ms>0x7fffffff)ms=0x7fffffff;
    p->endTime=ShipyardInput_InputMilliseconds()+ms;return 0;
}
void ShipyardInput_StopPhysicalRumble(){for(unsigned i=0;i<4;++i){ShipyardInput_WritePhysicalRumble(i,0,0);pads[i].timed=false;}}
int ShipyardInput_GamepadHasSensor(ShipyardInputGamepad*,int){return 0;}
int ShipyardInput_GamepadEnableSensor(ShipyardInputGamepad*,int,int){return unsupported();}
int ShipyardInput_GamepadSensorData(ShipyardInputGamepad*,int,float*,int){return unsupported();}
int ShipyardInput_GamepadHasLED(ShipyardInputGamepad*){return 0;}
int ShipyardInput_GamepadLED(ShipyardInputGamepad*,uint8_t,uint8_t,uint8_t){return unsupported();}
