#include "platform_keyboard.h"
static int keyboardStarts, keyboardStatus;
static wchar_t* keyboardOutput;
static unsigned keyboardCapacity;
static bool keyboardNumeric;
static bool keyboardStartSucceeds=true;
static int keyboardCancels;
static ImGuiInputTextFlags keyboardFlags=0;
static bool FakeStartKeyboard(unsigned slot, bool numeric, bool password, const wchar_t* initial, wchar_t* output, unsigned capacity) {
    ++keyboardStarts; keyboardOutput=output; keyboardCapacity=capacity; keyboardNumeric=numeric;
    CHECK(slot==2); CHECK(initial!=NULL);
    if(keyboardStarts==1)CHECK(initial[0]==L'o' && initial[7]==L'l' && initial[8]==0);
    return keyboardStartSucceeds;
}
static int FakePollKeyboard(){return keyboardStatus;}
static void FakeCancelKeyboard(){++keyboardCancels;}
static void RunKeyboardContract(){
    Shipyard::ImGuiPlatform::Keyboard keyboard;
    Shipyard::ImGuiPlatform::KeyboardTransport transport={FakeStartKeyboard,FakePollKeyboard,FakeCancelKeyboard};
    char text[128]="original";
    ImGuiID id=0;
    for(int i=0;i<3;++i){
        ImGui::NewFrame();ImGui::Begin("Keyboard contract");
        if(i==0)ImGui::SetKeyboardFocusHere();
        ImGui::InputText("value",text,sizeof(text),keyboardFlags); id=ImGui::GetItemID();
        ImGui::End();ImGui::EndFrame();
    }
    CHECK(ImGui::GetActiveID()==id); ImGui::GetIO().WantTextInput=true;
    keyboardStatus=0; CHECK(keyboard.Update(transport,2,true)); CHECK(keyboardStarts==1);
    keyboardOutput[0]=L'n';keyboardOutput[1]=L'e';keyboardOutput[2]=L'w';keyboardOutput[3]=0;
    keyboardStatus=1; CHECK(keyboard.Update(transport,2,true));
    ImGui::NewFrame();ImGui::Begin("Keyboard contract");ImGui::InputText("value",text,sizeof(text),keyboardFlags);ImGui::End();ImGui::EndFrame();
    CHECK(std::strcmp(text,"new")==0);
    keyboard.Update(transport,2,true);CHECK(keyboardStarts==1);
    keyboard.Reset(transport);keyboardStatus=0;keyboard.Update(transport,2,true);
    keyboardOutput[0]=L'x';keyboardOutput[1]=0;keyboardStatus=-1;keyboard.Update(transport,2,true);
    ImGui::NewFrame();ImGui::Begin("Keyboard contract");ImGui::InputText("value",text,sizeof(text),keyboardFlags);ImGui::End();ImGui::EndFrame();
    CHECK(std::strcmp(text,"new")==0);
    keyboard.Reset(transport);keyboardStatus=0;keyboard.Update(transport,2,true);
    keyboardOutput[0]=0;keyboardStatus=1;keyboard.Update(transport,2,true);
    ImGui::NewFrame();ImGui::Begin("Keyboard contract");ImGui::InputText("value",text,sizeof(text),keyboardFlags);ImGui::End();ImGui::EndFrame();
    CHECK(text[0]==0);
    keyboard.Reset(transport);keyboardStatus=0;keyboard.Update(transport,2,true);
    keyboardOutput[0]=0x00e9;keyboardOutput[1]=0;keyboardStatus=1;keyboard.Update(transport,2,true);
    ImGui::NewFrame();ImGui::Begin("Keyboard contract");ImGui::InputText("value",text,sizeof(text),keyboardFlags);ImGui::End();ImGui::EndFrame();
    CHECK(std::strcmp(text,"\xc3\xa9")==0);
    keyboard.Reset(transport);keyboardStatus=0;keyboard.Update(transport,2,true);
    keyboardOutput[0]=0xd83d;keyboardOutput[1]=0xde80;keyboardOutput[2]=0;keyboardStatus=1;keyboard.Update(transport,2,true);
    ImGui::NewFrame();ImGui::Begin("Keyboard contract");ImGui::InputText("value",text,sizeof(text),keyboardFlags);ImGui::End();ImGui::EndFrame();
#if IM_UNICODE_CODEPOINT_MAX > 0xffff
    CHECK(std::strcmp(text,"\xf0\x9f\x9a\x80")==0);
#else
    CHECK(std::strcmp(text,"\xef\xbf\xbd")==0);
#endif
    keyboard.Reset(transport);keyboardStatus=0;keyboard.Update(transport,2,true);
    ImGui::ClearActiveID();keyboardStatus=1;keyboard.Update(transport,2,true);
    CHECK(GImGui->InputEventsQueue.Size==0);
    keyboard.Reset(transport);
    // Re-activate with the numeric filter and verify native text still takes that path.
    keyboardFlags=ImGuiInputTextFlags_CharsDecimal;
    for(int i=0;i<3;++i){
        ImGui::NewFrame();ImGui::Begin("Keyboard contract");
        if(i==0)ImGui::SetKeyboardFocusHere();
        ImGui::InputText("value",text,sizeof(text),keyboardFlags);ImGui::End();ImGui::EndFrame();
    }
    ImGui::GetIO().WantTextInput=true;
    keyboardStatus=0;keyboard.Update(transport,2,true);CHECK(keyboardNumeric);
    keyboardOutput[0]=L'4';keyboardOutput[1]=L'x';keyboardOutput[2]=L'2';keyboardOutput[3]=0;
    keyboardStatus=1;keyboard.Update(transport,2,true);
    ImGui::NewFrame();ImGui::Begin("Keyboard contract");ImGui::InputText("value",text,sizeof(text),keyboardFlags);ImGui::End();ImGui::EndFrame();
    CHECK(std::strcmp(text,"42")==0);
    keyboard.Reset(transport);keyboardStartSucceeds=false;
    const int priorStarts=keyboardStarts;CHECK(!keyboard.Update(transport,2,true));
    CHECK(!keyboard.Update(transport,2,true));CHECK(keyboardStarts==priorStarts+1);
    CHECK(std::strcmp(text,"42")==0);keyboardStartSucceeds=true;
    keyboard.Reset(transport);keyboardStatus=0;CHECK(keyboard.Update(transport,2,true));
    const int priorCancels=keyboardCancels;keyboard.Reset(transport);CHECK(keyboardCancels==priorCancels+1);
    keyboardStatus=1;CHECK(keyboard.Update(transport,2,true)); // immediate native completion
    keyboardOutput[0]=L'7';keyboardOutput[1]=0;CHECK(keyboard.Update(transport,2,true));
    ImGui::NewFrame();ImGui::Begin("Keyboard contract");ImGui::InputText("value",text,sizeof(text),keyboardFlags);ImGui::End();ImGui::EndFrame();
    CHECK(std::strcmp(text,"7")==0);
    keyboard.Reset(transport);ImGui::ClearActiveID();
    // Exercise the real native frame wiring with the XDK calls supplied by fixtures.
    for(int i=0;i<3;++i){
        ImGui::NewFrame();ImGui::Begin("Keyboard contract");
        if(i==0)ImGui::SetKeyboardFocusHere();
        ImGui::InputText("value",text,sizeof(text),keyboardFlags);ImGui::End();ImGui::EndFrame();
    }
    ImGui::GetIO().WantTextInput=true;physicalSlot=2;physicalPad.buttons=XINPUT_GAMEPAD_A;
    Shipyard_ImGuiPlatformNewFrame(1280,720,true);CHECK(nativeStarts==1);
    ImGui::NewFrame();CHECK(!ImGui::IsMouseDown(0));CHECK(!ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown));
    ImGui::Begin("Keyboard contract");ImGui::InputText("value",text,sizeof(text),keyboardFlags);ImGui::End();ImGui::EndFrame();
    nativeOutput[0]=L'9';nativeOutput[1]=0;nativeResult=ERROR_SUCCESS;
    Shipyard_ImGuiPlatformNewFrame(1280,720,true);
    ImGui::NewFrame();ImGui::Begin("Keyboard contract");ImGui::InputText("value",text,sizeof(text),keyboardFlags);ImGui::End();ImGui::EndFrame();
    CHECK(std::strcmp(text,"9")==0);
    Shipyard_ImGuiPlatformNewFrame(1280,720,true);
    ImGui::NewFrame();CHECK(!ImGui::IsMouseDown(0));CHECK(nativeStarts==1);
    ImGui::Begin("Keyboard contract");ImGui::InputText("value",text,sizeof(text),keyboardFlags);ImGui::End();ImGui::EndFrame();
    physicalPad.buttons=0;ImGui::ClearActiveID();
    std::printf("Keyboard accept/cancel/empty/Unicode/stale-focus/numeric/failure/shutdown pass\n");
}
