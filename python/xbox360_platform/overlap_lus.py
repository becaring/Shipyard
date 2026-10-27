"""Game/render overlap projections for libultraship units shared by every consumer.

Render reads CVars from a snapshot captured while the game is idle at its handoff.
Resource owners follow the packet that read them; cache mutation waits for replay.
Consumers provide Shipyard_OverlapFence, Shipyard_OverlapIsGameWorker,
Shipyard_OverlapRunOnRender and Shipyard_OverlapRetainResource.
"""

import re

MAP = ("std::unordered_map<Shipyard::ConsoleVariables::NameKey, std::shared_ptr<CVar>, "
       "Shipyard::ConsoleVariables::NameHash>")
DECLARATIONS = """\
extern "C" int Shipyard_OverlapIsGameWorker(void);
extern "C" void Shipyard_OverlapFence(void);
extern "C" void Shipyard_OverlapRunOnRender(void (*callback)(void*), void* context);
"""
SNAPSHOT_METHODS = r"""
namespace {
__declspec(thread) const ConsoleVariable* overlapSnapshotOwner = nullptr;
__declspec(thread) const ConsoleVariable::Snapshot* overlapSnapshot = nullptr;
}
const ConsoleVariable::Snapshot& ConsoleVariable::ReadVariables() const {
    return overlapSnapshotOwner == this && overlapSnapshot ? *overlapSnapshot : mVariables;
}
void ConsoleVariable::UseSnapshot(const Snapshot* snapshot) const {
    overlapSnapshotOwner = snapshot ? this : nullptr;
    overlapSnapshot = snapshot;
}
void ConsoleVariable::CaptureSnapshot(Snapshot& snapshot) const {
    // Keep all names, including mod CVars. Reuse storage only after replay retires.
    for (Snapshot::iterator cursor = snapshot.begin(); cursor != snapshot.end();) {
        Snapshot::const_iterator live = mVariables.find(cursor->first);
        if (live == mVariables.end() || !live->second) cursor = snapshot.erase(cursor);
        else ++cursor;
    }
    for (Snapshot::const_iterator cursor = mVariables.begin(); cursor != mVariables.end(); ++cursor) {
        if (!cursor->second) continue;
        const CVar& original = *cursor->second;
        std::shared_ptr<CVar>& copy = snapshot[cursor->first];
        // Get() may have handed ownership to a caller beyond the previous replay.
        if (!copy || copy.use_count() != 1) copy = std::make_shared<CVar>();
        if (copy->Type == ConsoleVariableType::String) {
            if (original.Type == ConsoleVariableType::String &&
                (copy->String == original.String || (copy->String && original.String &&
                 strcmp(copy->String, original.String) == 0))) continue;
            free(copy->String);
            copy->String = nullptr;
        }
        copy->Type = original.Type;
        switch (original.Type.GetValue()) {
            case ConsoleVariableType::Integer: copy->Integer = original.Integer; break;
            case ConsoleVariableType::Float: copy->Float = original.Float; break;
            case ConsoleVariableType::String:
                copy->String = original.String ? strdup(original.String) : nullptr;
                break;
            case ConsoleVariableType::Color: copy->Color = original.Color; break;
            case ConsoleVariableType::Color24: copy->Color24 = original.Color24; break;
        }
    }
}
"""


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError("Overlap projection anchor drift: " + old)
    return text.replace(old, new)


def console_variable_header(text, container=MAP):
    """Run after console_variable_lookup.header, or pass the upstream container type."""
    text = once(text, "    ConsoleVariable();",
                "    typedef " + container + " Snapshot;\n"
                "    void CaptureSnapshot(Snapshot& snapshot) const;\n"
                "    void UseSnapshot(const Snapshot* snapshot) const;\n    ConsoleVariable();")
    return once(text, "    " + container + " mVariables;",
                "    const Snapshot& ReadVariables() const;\n    Snapshot mVariables;")


LOOKUP = ("mVariables.find(Shipyard::ConsoleVariables::NameKey(name, "
          "Shipyard::ConsoleVariables::NameKey::Borrow))")


def console_variable_source(text, value="GetValue()", lookup=LOOKUP, readers=6):
    """Run after console_variable_lookup.transform, or pass upstream lookups; only getters read the snapshot."""
    text = once(text, "namespace Ship {", "namespace Ship {\n" +
                SNAPSHOT_METHODS.replace("GetValue()", value))
    if text.count(lookup) != readers:
        raise ValueError("CVar reader projection drift")
    expected = readers
    readers, setters = text.split("void ConsoleVariable::SetInteger", 1)
    for local in ("it", "found"):
        readers = readers.replace(
            "    auto " + local + " = " + lookup,
            "    const Snapshot& variables = ReadVariables();\n    auto " + local + " = "
            + lookup.replace("mVariables", "variables"))
    readers = readers.replace("!= mVariables.end()", "!= variables.end()")
    # Snapshot capture itself iterates the live map while game and audio are idle.
    readers = readers.replace("cursor != variables.end()", "cursor != mVariables.end()")
    if readers.count("ReadVariables();") != expected:
        raise ValueError("CVar reader projection drift")
    return readers + "void ConsoleVariable::SetInteger" + setters


def _function_start(text, pattern):
    starts = [m.end() for m in re.finditer(pattern, text)]
    if not starts:
        raise ValueError("Overlap function drift: " + pattern)
    return starts


def _forwarding_returns(text):
    """Forwarded owners were retained by the function that produced them.

    GetCachedResource and LoadResourceProcess retain their own results; an
    inline miss (resource_lookup.synchronous_misses) makes LoadResource's loaded
    result one of those. A future's result came from a pool worker, which
    retains nothing, so LoadResource keeps retaining it.
    """
    for signature, names in (
        ("ResourceManager::LoadResource(const ResourceIdentifier& identifier", ("cachedResource", "resource")),
        ("ResourceManager::LoadResourceProcess(const ResourceIdentifier& identifier", ("cachedResource",)),
    ):
        if text.count(signature) != 1:
            raise ValueError("Resource forwarding drift: " + signature)
        begin = text.index(signature)
        end = text.index("\n}", begin)
        body = text[begin:end]
        forwarded = 0
        for name in names:
            kept = "return Shipyard_OverlapKeepResource(" + name + ");"
            if name == "resource" and ").get()" in body:
                continue
            if body.count(kept) > 1:
                raise ValueError("Resource forwarding drift: " + signature)
            forwarded += body.count(kept)
            body = body.replace(kept, "return " + name + ";")
        if not forwarded and "LoadResourceProcess" in signature:
            raise ValueError("Resource forwarding drift: " + signature)
        text = text[:begin] + body + text[end:]
    return text


def resource_manager(text):
    """Retain every returned owner; cache mutation and unload wait for replay."""
    # Rewrite returns before adding the helper, whose own return must stay plain.
    text, count = re.subn(r"return (resource|cachedResource);",
                          r"return Shipyard_OverlapKeepResource(\1);", text)
    if not count:
        raise ValueError("Resource return drift")
    text = _forwarding_returns(text)
    text = "#include <memory>\n" + DECLARATIONS + (
        "namespace Ship { class IResource; }\n"
        "void Shipyard_OverlapRetainResource(const std::shared_ptr<Ship::IResource>& resource);\n"
        "static inline std::shared_ptr<Ship::IResource> Shipyard_OverlapKeepResource(\n"
        "    const std::shared_ptr<Ship::IResource>& resource) {\n"
        "    Shipyard_OverlapRetainResource(resource);\n    return resource;\n}\n") + text
    pattern = r"\bResourceManager::(?:UnloadResource|WriteResource|SetAltAssetsEnabled)\([^)]*\)\s*\{"
    for offset in reversed(_function_start(text, pattern)):
        text = text[:offset] + "\n    Shipyard_OverlapFence();" + text[offset:]
    # Directory mutation completes on its caller so a queued write cannot outlive its fence.
    unload = "mThreadPool->submit_task([this, filter]() -> void { UnloadResourcesProcess(filter); }, priority);"
    text = once(text, unload, "if (Shipyard_OverlapIsGameWorker()) {\n"
                "        Shipyard_OverlapFence();\n        UnloadResourcesProcess(filter);\n"
                "    } else {\n        " + unload + "\n    }")
    signature = "void ResourceManager::DirtyResources(const ResourceFilter& filter) {"
    start = text.index(signature) + len(signature)
    end = text.index("\n}\n", start)
    body = text[start:end]
    task = "    mThreadPool->submit_task([this, filter]() -> void {"
    if body.count(task) != 1 or not body.endswith("\n    });"):
        raise ValueError("Resource dirty task drift")
    lead, work = body.split(task, 1)
    inline = "\n        Shipyard_OverlapFence();\n        {" + work[:-len("\n    });")] + "\n        }"
    body = (lead + "    if (Shipyard_OverlapIsGameWorker()) {" + inline + "\n        return;\n    }\n"
            + task + work)
    return text[:start] + body + text[end:]


def texture_cache_clear(text):
    """Interpreter texture cache clears from the game run as render jobs."""
    signature = "void Interpreter::TextureCacheClear() {"
    return once(DECLARATIONS + text, signature, signature + """
    if (Shipyard_OverlapIsGameWorker()) {
        struct RenderClear {
            Interpreter* interpreter;
            static void Run(void* opaque) {
                static_cast<RenderClear*>(opaque)->interpreter->TextureCacheClear();
            }
        } job = { this };
        Shipyard_OverlapRunOnRender(RenderClear::Run, &job);
        return;
    }""")


def control_deck(text):
    """Game-worker input-blocked queries answer from render's handoff snapshot.

    The originals read live ImGui and GUI state that render rebuilds while the
    game runs; overlap/input_blocked.h captures them on render at each handoff.
    """
    text = 'extern "C" int Shipyard_OverlapInputBlocked(unsigned kind, int* value);\n' + text
    for kind, method in enumerate(("Gamepad", "Keyboard", "Mouse")):
        signature = "bool ControlDeck::" + method + "GameInputBlocked() {"
        text = once(text, signature, signature + "\n    int blocked;\n"
                    "    if (Shipyard_OverlapInputBlocked(%d, &blocked))\n"
                    "        return AllGameInputBlocked() || blocked != 0;" % kind)
    return text


# Passive GUI: one GUI build per packet at the idle handoff, replayed from a capture.
GUI_DECLARATIONS = """\
    bool XboxHasInteractiveGui();
    bool XboxBeginOverlapDraw();
    void XboxFinishPassiveDraw();
    void XboxCommitOverlapIntent();
    void XboxDrawPassiveMenu();
    virtual void XboxDrawPassiveGame();
"""
GUI_HOOKS = """\
#include <stdint.h>
struct ImDrawData;
extern "C" int Shipyard_OverlapDrawGui(int begin);
bool Shipyard_ImGuiCaptureDrawData(ImDrawData* data);
uintptr_t Shipyard_ImGuiCaptureGameTexture(uintptr_t texture);
"""
INTENT_KEYS = (("TOGGLE_BTN", "overlapF1"), ("ImGuiKey_Escape", "overlapEscape"),
               ("TOGGLE_PAD_BTN", "overlapBack"))
RESET_KEYS = ("(ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)) &&\n"
              "        ImGui::IsKeyPressed(ImGuiKey_R, false)")


def public_method(text, cls, code):
    """Insert members at the start of a class's first public section."""
    matches = list(re.finditer(r"class " + cls + r"\b[^;{]*\{\s*public:\n", text))
    if len(matches) != 1:
        raise ValueError("Overlap projection class drift: " + cls)
    end = matches[0].end()
    return text[:end] + "    " + code.strip() + "\n" + text[end:]


def _method(text, signature):
    start = text.index(signature)
    if text.count(signature) != 1:
        raise ValueError("Overlap projection method drift: " + signature)
    return start, text.index("\n}\n", start) + 3


def gui_header(text, anchor="    void StartDraw();"):
    return once(text, anchor, anchor + "\n" + GUI_DECLARATIONS.rstrip("\n"))


def gui_window_header(text):
    """Visible windows that cannot be replayed from a capture keep the GUI live."""
    return public_method(text, "GuiWindow", """virtual bool XboxCanDrawDuringOverlap() { return false; }
    virtual bool XboxHasPendingFrameWork() {
        return !IsInitialized() || (IsVisible() && !XboxCanDrawDuringOverlap());
    }""")


def overlap_drawable(text, cls):
    return public_method(text, cls, "bool XboxCanDrawDuringOverlap() { return true; }")


def pending_frame_work(text, cls, condition):
    return public_method(text, cls, "bool XboxHasPendingFrameWork() { return !IsInitialized() || " +
                         condition + "; }")


def game_overlay_header(text):
    """Overlay timers tick per presentation, so live overlays keep the GUI sequential."""
    return public_method(text, "GameOverlay", """bool XboxHasPendingDraw() const {
        return !mRegisteredOverlays.empty() || mNeedsCleanup;
    }""")


def game_overlay_source(text):
    return once(text, "            overlay.duration -= .05f;",
                "            overlay.duration -= .05f;\n"
                "            if (overlay.duration <= 0.0f) mNeedsCleanup = true;")


def gui_capture(text, framebuffer):
    """Name the game image through the capture so replays compose their own frame."""
    if not framebuffer.startswith("    uintptr_t fb = ") or not framebuffer.endswith(";"):
        raise ValueError("Overlap projection framebuffer form changed")
    expression = framebuffer[len("    uintptr_t fb = "):-1]
    return once(text, framebuffer, "    uintptr_t fb = Shipyard_ImGuiCaptureGameTexture(\n        " +
                expression + ");")


def gui_passive_game(text, cls):
    """XboxDrawPassiveGame is DrawGame without the per-presentation overlay."""
    start, end = _method(text, "void " + cls + "::DrawGame() {")
    game = text[start:end].replace(cls + "::DrawGame", cls + "::XboxDrawPassiveGame", 1)
    overlay = "    GetGameOverlay()->Draw();\n"
    if game.count(overlay) > 1:
        raise ValueError("Overlap projection overlay drift")
    return text[:end] + "\n" + game.replace(overlay, "") + text[end:]


def gui_passive(text, cls="Gui"):
    """Split hotkey intents out of DrawMenu and add the passive GUI entry points.

    Apply before range lowering and before StartDraw is otherwise projected.
    """
    instance = "GetRawInstance" if "Context::GetRawInstance()" in text else "GetInstance"
    start, end = _method(text, "void " + cls + "::DrawMenu() {")
    menu = text[start:end]
    intent_start = menu.index("    if (ImGui::IsKeyPressed(TOGGLE_BTN, false)")
    intent_end = menu.index("    if (GetMenuBar()) {", intent_start)
    intent = menu[intent_start:intent_end]
    for key, saved in INTENT_KEYS:
        if "ImGui::IsKeyPressed(" + key + ", false)" not in intent:
            raise ValueError("Overlap projection intent drift: " + key)
        intent = intent.replace("ImGui::IsKeyPressed(" + key + ", false)", saved)
    intent = once(intent, RESET_KEYS, "overlapReset")
    passive = (menu[:intent_start] + menu[intent_end:]).replace(
        cls + "::DrawMenu", cls + "::XboxDrawPassiveMenu", 1)
    draw_start, draw_end = _method(text, "void " + cls + "::StartDraw() {")
    draw = text[draw_start:draw_end]
    remainder = draw[draw.index("    DrawMenu();\n") + len("    DrawMenu();\n"):draw.rindex("}")]
    entry = r"""
static bool overlapF1, overlapEscape, overlapBack, overlapReset;

bool CLS::XboxHasInteractiveGui() {
    if (mNeedsConsoleVariableSave) return true;
    if (GetGameOverlay()->XboxHasPendingDraw()) return true;
    if (GetMenuOrMenubarVisible() || ImGui::GetTopMostPopupModal() != nullptr) return true;
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) return true;
    for (auto window = mGuiWindows.begin(); window != mGuiWindows.end(); ++window) {
        if (window->second->XboxHasPendingFrameWork()) return true;
    }
    return false;
}

// Render, game idle: intents stop the session and replay after the worker joins.
bool CLS::XboxBeginOverlapDraw() {
    StartFrame();
    overlapF1 = ImGui::IsKeyPressed(TOGGLE_BTN, false);
    overlapEscape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    overlapBack = ImGui::IsKeyPressed(TOGGLE_PAD_BTN, false);
    const bool toggle = overlapF1 || overlapEscape ||
        (overlapBack && Context::INSTANCE()->GetConsoleVariables()->GetInteger(CVAR_IMGUI_CONTROLLER_NAV, 0));
    overlapReset = RESET;
    return !XboxHasInteractiveGui() && !toggle && !overlapReset;
}

void CLS::XboxCommitOverlapIntent() {
INTENT}

void CLS::XboxFinishPassiveDraw() {
    XboxDrawPassiveMenu();
REMAINDER    XboxDrawPassiveGame();
    EndFrame();
}

""".replace("CLS", cls).replace("INSTANCE", instance).replace("RESET", RESET_KEYS).replace(
        "INTENT", intent).replace("REMAINDER", remainder)
    text = text[:end] + "\n" + passive + entry + text[end:]
    text = once(text, "void " + cls + "::StartDraw() {",
                "void " + cls + "::StartDraw() {\n    if (Shipyard_OverlapDrawGui(1)) return;")
    text = once(text, "void " + cls + "::EndDraw() {",
                "void " + cls + "::EndDraw() {\n    if (Shipyard_OverlapDrawGui(0)) return;")
    return GUI_HOOKS + text


def graphics_snapshot(text, getters):
    """Game-worker graphics queries answer from render's handoff snapshot.

    getters: (name, return type, default) for each `extern "C" type name() {` export.
    Render calls Shipyard_OverlapCaptureGraphics while the game waits at its handoff.
    """
    fields, capture = [], []
    for index, (name, kind, default) in enumerate(getters):
        signature = 'extern "C" ' + kind + " " + name + "() {"
        text = once(text, signature, signature + "\n    if (Shipyard_OverlapIsGameWorker()) "
                    "return shipyardGraphics.value%d;" % index)
        fields.append("    %s value%d;" % (kind, index))
        capture.append("    shipyardGraphics.value%d = %s();" % (index, name))
    defaults = ", ".join(default for _, _, default in getters)
    return ('#include <stdint.h>\nextern "C" int Shipyard_OverlapIsGameWorker(void);\n'
            "static struct ShipyardGraphicsSnapshot {\n" + "\n".join(fields) + "\n} shipyardGraphics = { " +
            defaults + " };\n" + text +
            '\nextern "C" void Shipyard_OverlapCaptureGraphics(void) {\n' + "\n".join(capture) + "\n}\n")
