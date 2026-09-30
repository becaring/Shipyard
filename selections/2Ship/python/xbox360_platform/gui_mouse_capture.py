"""Snapshot callback-free mouse-capture inputs for one ImGui window pass."""


def transform(text):
    start = text.index('void Gui::HandleMouseCapture() {')
    end = text.index('\n}', start) + 2
    body = text[start:end]
    expressions = {
        'GetMainGameWindowID()': 'mainWindowId',
        'GetGameOverlay()->GetID()': 'overlayWindowId',
        'Context::GetInstance()->GetWindow()->IsMouseCaptured()': 'mouseCaptured',
    }
    for expression, name in expressions.items():
        if body.count(expression) != 1:
            raise ValueError('GUI mouse capture source drift: ' + expression)
        body = body.replace(expression, name)
    anchor = '    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMouseInputs;'
    if body.count(anchor) != 1:
        raise ValueError('GUI mouse capture flags drift')
    body = body.replace(anchor, anchor + '\n'
        '    ImGuiID mainWindowId = 0, overlayWindowId = 0;\n'
        '    bool mainKnown = false, overlayKnown = false, captureKnown = false;\n'
        '    bool mouseCaptured = false;')
    condition = '        if (windowIter.key != mainWindowId && windowIter.key != overlayWindowId) {'
    if body.count(condition) != 1:
        raise ValueError('GUI mouse capture eligibility drift')
    body = body.replace(condition,
        '        if (!mainKnown) { mainWindowId = GetMainGameWindowID(); mainKnown = true; }\n'
        '        if (windowIter.key == mainWindowId) continue;\n'
        '        if (!overlayKnown) { overlayWindowId = GetGameOverlay()->GetID(); overlayKnown = true; }\n'
        '        if (windowIter.key != overlayWindowId) {')
    if body.count('            if (mouseCaptured) {') != 1:
        raise ValueError('GUI mouse capture use drift')
    body = body.replace('            if (mouseCaptured) {',
        '            if (!captureKnown) {\n'
        '                mouseCaptured = Context::GetInstance()->GetWindow()->IsMouseCaptured();\n'
        '                captureKnown = true;\n'
        '            }\n'
        '            if (mouseCaptured) {')
    return text[:start] + body + text[end:]
