void Button(ImGuiIO& io, ImGuiKey key, WORD buttons, WORD bit) {
    io.AddKeyEvent(key, (buttons & bit) != 0);
}

void Axis(ImGuiIO& io, ImGuiKey key, float value) {
    value = std::max(0.0f, std::min(1.0f, value));
    io.AddKeyAnalogEvent(key, value > .1f, value);
}

template<class Pad> void BindGamepad(ImGuiIO& io, const Pad& pad) {
    WORD nav = pad.buttons;
    Button(io, ImGuiKey_GamepadBack, nav, XINPUT_GAMEPAD_BACK);
    Button(io, ImGuiKey_GamepadStart, nav, XINPUT_GAMEPAD_START);
    Button(io, ImGuiKey_GamepadFaceDown, nav, XINPUT_GAMEPAD_A);
    Button(io, ImGuiKey_GamepadFaceRight, nav, XINPUT_GAMEPAD_B);
    Button(io, ImGuiKey_GamepadFaceLeft, nav, XINPUT_GAMEPAD_X);
    Button(io, ImGuiKey_GamepadFaceUp, nav, XINPUT_GAMEPAD_Y);
    Button(io, ImGuiKey_GamepadDpadLeft, nav, XINPUT_GAMEPAD_DPAD_LEFT);
    Button(io, ImGuiKey_GamepadDpadRight, nav, XINPUT_GAMEPAD_DPAD_RIGHT);
    Button(io, ImGuiKey_GamepadDpadUp, nav, XINPUT_GAMEPAD_DPAD_UP);
    Button(io, ImGuiKey_GamepadDpadDown, nav, XINPUT_GAMEPAD_DPAD_DOWN);
    Button(io, ImGuiKey_GamepadL1, nav, XINPUT_GAMEPAD_LEFT_SHOULDER);
    Button(io, ImGuiKey_GamepadR1, nav, XINPUT_GAMEPAD_RIGHT_SHOULDER);
    Button(io, ImGuiKey_GamepadL3, nav, XINPUT_GAMEPAD_LEFT_THUMB);
    Button(io, ImGuiKey_GamepadR3, nav, XINPUT_GAMEPAD_RIGHT_THUMB);
    float x = pad.leftX, y = pad.leftY;
    Axis(io, ImGuiKey_GamepadLStickLeft, (-x - 8000) / 24767);
    Axis(io, ImGuiKey_GamepadLStickRight, (x - 8000) / 24767);
    Axis(io, ImGuiKey_GamepadLStickUp, (y - 8000) / 24767);
    Axis(io, ImGuiKey_GamepadLStickDown, (-y - 8000) / 24767);
    Axis(io, ImGuiKey_GamepadL2, pad.leftTrigger / 255.0f);
    Axis(io, ImGuiKey_GamepadR2, pad.rightTrigger / 255.0f);
}
