#include "platform_frame.h"
#include "input/physical.h"
#include "imgui.h"
#include <algorithm>
#include <cstring>
#include <float.h>
#include <math.h>
#ifdef _XBOX
#include <xtl.h>
#endif

namespace {
Shipyard::ImGuiPlatform::Services services;
double lastFrame;
WORD previousButtons;
WORD blockedPointerButtons;
unsigned previousSlot=4;
bool previousMenu;
bool previousPointerActive;
bool pointerMode=true;
ImVec2 pointer(640,360);
#include "gamepad_bindings.inc"
}

namespace Shipyard { namespace ImGuiPlatform {

void Init(const Services& configured){
    services=configured;
    ImGui::GetIO().BackendPlatformName=services.backendName;
    lastFrame=services.now();
    previousButtons=blockedPointerButtons=0;
    previousSlot=4;
    previousMenu=previousPointerActive=false;
    pointerMode=true;
    pointer=ImVec2(640,360);
}

void Shutdown(){
    ImGuiIO& io=ImGui::GetIO();
    io.ClearInputKeys();
    io.ClearInputMouse();
    io.MouseDrawCursor=false;
    io.BackendFlags&=~ImGuiBackendFlags_HasGamepad;
    io.BackendPlatformName=NULL;
}

void NewFrame(unsigned width,unsigned height,bool menuVisible){
    ImGuiIO& io=ImGui::GetIO();
    const double now=services.now();
    io.DeltaTime=static_cast<float>(std::max(.000001,now-lastFrame));lastFrame=now;
    io.DisplaySize=ImVec2(static_cast<float>(width),static_cast<float>(height));
    io.DisplayFramebufferScale=ImVec2(1,1);
    services.prepareInput();
    ShipyardInputRawPad pad={0};
    bool connected=false;
    unsigned selectedSlot=4;
    for(unsigned slot=0;slot<4;++slot){
        if(ShipyardInput_ReadCachedGamepad(slot,&pad)){connected=true;selectedSlot=slot;break;}
    }
    if(connected)io.BackendFlags|=ImGuiBackendFlags_HasGamepad;
    else {io.BackendFlags&=~ImGuiBackendFlags_HasGamepad;std::memset(&pad,0,sizeof(pad));}

    const bool sourceChanged=selectedSlot!=previousSlot;
    const bool opened=menuVisible&&!previousMenu;
    if(opened)pointerMode=true;
    if(sourceChanged)previousButtons=pad.buttons;
    if(menuVisible&&connected&&!opened&&
       (pad.buttons&XINPUT_GAMEPAD_RIGHT_THUMB)&&!(previousButtons&XINPUT_GAMEPAD_RIGHT_THUMB))
        pointerMode=!pointerMode;

    const bool pointerActive=connected&&menuVisible&&pointerMode&&
                             (io.ConfigFlags&ImGuiConfigFlags_NavEnableGamepad)!=0;
    if(pointerActive!=previousPointerActive||sourceChanged)
        blockedPointerButtons=pad.buttons&(XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B);
    blockedPointerButtons&=pad.buttons;

    ShipyardInputRawPad navPad=pad;
    if(menuVisible&&pointerMode){
        std::memset(&navPad,0,sizeof(navPad));
        navPad.buttons=pad.buttons&XINPUT_GAMEPAD_BACK;
    }
    BindGamepad(io,navPad);
    if(pointerActive){
        const float maxX=width?static_cast<float>(width-1):0.0f;
        const float maxY=height?static_cast<float>(height-1):0.0f;
        const float x=pad.rightX,y=pad.rightY;
        if(fabsf(x)>8000)pointer.x+=x/32768.0f*900*io.DeltaTime;
        if(fabsf(y)>8000)pointer.y-=y/32768.0f*900*io.DeltaTime;
        pointer.x=std::max(0.0f,std::min(maxX,pointer.x));
        pointer.y=std::max(0.0f,std::min(maxY,pointer.y));
        io.AddMousePosEvent(pointer.x,pointer.y);
        const WORD clicks=pad.buttons&~blockedPointerButtons;
        io.AddMouseButtonEvent(0,(clicks&XINPUT_GAMEPAD_A)!=0);
        io.AddMouseButtonEvent(1,(clicks&XINPUT_GAMEPAD_B)!=0);
        const float scroll=(pad.leftTrigger-pad.rightTrigger)/255.0f;
        if(fabsf(scroll)>.1f)io.AddMouseWheelEvent(0,scroll*8*io.DeltaTime);
    }else{
        io.AddMouseButtonEvent(0,false);
        io.AddMouseButtonEvent(1,false);
        io.AddMousePosEvent(-FLT_MAX,-FLT_MAX);
    }
    io.MouseDrawCursor=pointerActive;
    previousButtons=pad.buttons;
    previousSlot=selectedSlot;
    previousMenu=menuVisible;
    previousPointerActive=pointerActive;
}

void DrawCursor(){
    if(!previousPointerActive)return;
    ImGuiIO& io=ImGui::GetIO();
    const ImVec2 p=io.MousePos;
    if(p.x<0||p.y<0)return;
    // The native Xenos path cannot reliably render ImGui's baked cursor atlas.
    ImDrawList* draw=ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
    draw->AddTriangleFilled(p,ImVec2(p.x+1,p.y+25),ImVec2(p.x+18,p.y+17),IM_COL32(0,0,0,255));
    draw->AddTriangleFilled(ImVec2(p.x+3,p.y+4),ImVec2(p.x+4,p.y+20),
                            ImVec2(p.x+14,p.y+16),IM_COL32(255,255,255,255));
    io.MouseDrawCursor=false;
}

} }
