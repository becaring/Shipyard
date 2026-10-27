"""Passive overlap GUI projections: hotkey intents replay after the session, live GUI
work keeps frames sequential, and the projected methods compile and behave on host."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))

from xbox360_platform import lus_gui, overlap_lus  # noqa: E402

WINDOW_HEADER = """\
class GuiWindow : public GuiElement {
  public:
    GuiWindow(bool v, bool d) : GuiElement(v), drawable(d) {}
    virtual void Update() { ++updates; }
    virtual void Draw() { if (IsVisible()) ++draws; }
    bool drawable;
    int updates = 0, draws = 0;
};
class StatsWindow : public GuiWindow {
  public:
    StatsWindow() : GuiWindow(true, true) {}
};
class GameOverlay {
  public:
    void Draw() { ++draws; }
    int draws = 0;
    std::map<std::string, int> mRegisteredOverlays;
    bool mNeedsCleanup = false;
};
"""

GUI_HEADER = """\
class Gui {
  public:
    void StartDraw();
    void EndDraw();
    void DrawMenu();
    void StartFrame() { ++frames; }
    void CalculateGameViewport() { ++viewports; }
    void DrawGame();
    void EndFrame() { ++ends; }
    void CheckSaveCvars() {}
    bool GetMenuOrMenubarVisible() { return menubar.IsVisible(); }
    GuiElement* GetMenuBar() { return &menubar; }
    GuiElement* GetMenu() { return &menu; }
    GameOverlay* GetGameOverlay() { return &overlay; }
    GuiElement menubar, menu;
    GameOverlay overlay;
    std::map<std::string, std::shared_ptr<GuiWindow>> mGuiWindows;
    bool mNeedsConsoleVariableSave = false;
    int frames = 0, viewports = 0, ends = 0, resets = 0, games = 0;
};
"""

GUI_SOURCE = """\
void Gui::DrawMenu() {
    ImGui::Begin("Main - Deck");

    if (ImGui::IsKeyPressed(TOGGLE_BTN, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
        (ImGui::IsKeyPressed(TOGGLE_PAD_BTN, false) &&
         Ship::Context::GetInstance()->GetConsoleVariables()->GetInteger(CVAR_IMGUI_CONTROLLER_NAV, 0))) {
        if ((ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(TOGGLE_PAD_BTN, false)) && GetMenu()) {
            GetMenu()->ToggleVisibility();
        } else if ((ImGui::IsKeyPressed(TOGGLE_BTN, false) || ImGui::IsKeyPressed(TOGGLE_PAD_BTN, false)) &&
                   GetMenuBar()) {
            GetMenuBar()->ToggleVisibility();
        }
    }

    if ((ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)) &&
        ImGui::IsKeyPressed(ImGuiKey_R, false)) {
        ++resets;
    }

    if (GetMenuBar()) {
        GetMenuBar()->Update();
    }

    for (auto& windowIter : mGuiWindows) {
        windowIter.second->Update();
        windowIter.second->Draw();
    }

    ImGui::End();
}

void Gui::DrawGame() {
    GetGameOverlay()->Draw();
    uintptr_t fb = Ship::Context::GetInstance()->GetWindow()->GetGfxFrameBuffer();
    games += fb != 0;
}

void Gui::StartDraw() {
    // Initialize the frame.
    StartFrame();
    // Draw the gui menus
    DrawMenu();
    // Calculate the available space the game can render to
    CalculateGameViewport();
}

void Gui::EndDraw() {
    DrawGame();
    EndFrame();
    CheckSaveCvars();
}
"""

PRELUDE = """\
#include <cassert>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
enum { ImGuiKey_F1, ImGuiKey_Escape, ImGuiKey_GamepadBack, ImGuiKey_LeftCtrl, ImGuiKey_RightCtrl, ImGuiKey_R };
enum { ImGuiConfigFlags_ViewportsEnable = 1 };
#define TOGGLE_BTN ImGuiKey_F1
#define TOGGLE_PAD_BTN ImGuiKey_GamepadBack
#define CVAR_IMGUI_CONTROLLER_NAV "nav"
static bool pressed[6];
static int popups, drawGui, capturedTextures;
struct ImDrawData {};
namespace ImGui {
struct IO { int ConfigFlags; };
static IO io;
inline bool IsKeyPressed(int key, bool) { return pressed[key]; }
inline bool IsKeyDown(int key) { return pressed[key]; }
inline void* GetTopMostPopupModal() { return popups ? &popups : nullptr; }
inline IO& GetIO() { return io; }
inline void Begin(const char*) {}
inline void End() {}
}
namespace Ship {
struct Variables { int GetInteger(const char*, int) { return 1; } };
struct Window { uintptr_t GetGfxFrameBuffer() { return 9; } };
struct Context {
    static Context* GetInstance() { static Context context; return &context; }
    Variables* GetConsoleVariables() { static Variables variables; return &variables; }
    Window* GetWindow() { static Window window; return &window; }
};
}
using namespace Ship;
class GuiElement {
  public:
    explicit GuiElement(bool v = false) : visible(v) {}
    virtual ~GuiElement() {}
    bool IsVisible() { return visible; }
    bool IsInitialized() { return true; }
    void ToggleVisibility() { visible = !visible; }
    virtual void Update() {}
    bool visible;
};
"""

MAIN = """\
extern "C" int Shipyard_OverlapDrawGui(int begin) { return drawGui && begin >= 0; }
bool Shipyard_ImGuiCaptureDrawData(ImDrawData*) { return false; }
uintptr_t Shipyard_ImGuiCaptureGameTexture(uintptr_t texture) { ++capturedTextures; return texture; }
int main() {
    Gui gui;
    std::shared_ptr<GuiWindow> hidden(new GuiWindow(false, false));
    std::shared_ptr<GuiWindow> stats(new StatsWindow());
    gui.mGuiWindows["hidden"] = hidden;
    gui.mGuiWindows["stats"] = stats;
    assert(!gui.XboxHasInteractiveGui());
    assert(gui.XboxBeginOverlapDraw());
    gui.XboxFinishPassiveDraw();
    // One passive build: hidden work keeps its cadence; the overlay is not drawn.
    assert(gui.frames == 1 && gui.viewports == 1 && gui.ends == 1 && gui.games == 1);
    assert(hidden->updates == 1 && stats->draws == 1 && gui.overlay.draws == 0 && capturedTextures == 1);
    pressed[ImGuiKey_F1] = true;
    assert(!gui.XboxBeginOverlapDraw());
    gui.XboxFinishPassiveDraw();
    assert(!gui.menubar.IsVisible());
    pressed[ImGuiKey_F1] = false;
    gui.XboxCommitOverlapIntent();
    assert(gui.menubar.IsVisible() && gui.XboxHasInteractiveGui());
    gui.menubar.ToggleVisibility();
    pressed[ImGuiKey_LeftCtrl] = pressed[ImGuiKey_R] = true;
    assert(!gui.XboxBeginOverlapDraw());
    gui.XboxFinishPassiveDraw();
    assert(gui.resets == 0);
    gui.XboxCommitOverlapIntent();
    assert(gui.resets == 1);
    pressed[ImGuiKey_LeftCtrl] = pressed[ImGuiKey_R] = false;
    gui.mGuiWindows["editor"] = std::shared_ptr<GuiWindow>(new GuiWindow(true, false));
    assert(gui.XboxHasInteractiveGui());
    gui.mGuiWindows.erase("editor");
    gui.overlay.mRegisteredOverlays["note"] = 1;
    assert(gui.XboxHasInteractiveGui());
    gui.overlay.mRegisteredOverlays.clear();
    popups = 1;
    assert(gui.XboxHasInteractiveGui());
    popups = 0;
    drawGui = 1;
    const int frames = gui.frames;
    gui.StartDraw();
    gui.EndDraw();
    assert(gui.frames == frames);
    puts("passive gui: intents deferred, live work sequential");
}
"""


def projected_program():
    header = overlap_lus.gui_window_header(WINDOW_HEADER.split("class StatsWindow")[0])
    stats = overlap_lus.overlap_drawable("class StatsWindow" + WINDOW_HEADER.split("class StatsWindow")[1]
                                         .split("class GameOverlay")[0], "StatsWindow")
    overlay = overlap_lus.game_overlay_header("class GameOverlay" + WINDOW_HEADER.split("class GameOverlay")[1])
    gui = overlap_lus.gui_header(GUI_HEADER)
    source = overlap_lus.gui_capture(GUI_SOURCE, "    uintptr_t fb = Ship::Context::GetInstance()->GetWindow()"
                                     "->GetGfxFrameBuffer();")
    source = overlap_lus.gui_passive_game(overlap_lus.gui_passive(source), "Gui")
    return PRELUDE + header + stats + overlay + gui + source + MAIN


class PassiveGuiTests(unittest.TestCase):
    def test_intent_split(self):
        source = overlap_lus.gui_passive(GUI_SOURCE)
        passive = source[source.index("void Gui::XboxDrawPassiveMenu() {"):]
        passive = passive[:passive.index("\n}\n")]
        self.assertNotIn("IsKeyPressed", passive)
        self.assertIn("windowIter.second->Update();", passive)
        commit = source[source.index("void Gui::XboxCommitOverlapIntent() {"):]
        commit = commit[:commit.index("\n}\n")]
        self.assertIn("(overlapEscape || overlapBack) && GetMenu()", commit)
        self.assertIn("if (overlapReset) {", commit)
        self.assertEqual(source.count("if (Shipyard_OverlapDrawGui("), 2)
        with self.assertRaises(ValueError):
            overlap_lus.gui_passive(GUI_SOURCE.replace("ImGuiKey_Escape", "ImGuiKey_Tab"))

    def test_passive_game_drops_only_the_overlay(self):
        source = overlap_lus.gui_passive_game(GUI_SOURCE, "Gui")
        game = source[source.index("void Gui::XboxDrawPassiveGame() {"):]
        game = game[:game.index("\n}\n")]
        self.assertNotIn("GetGameOverlay()->Draw();", game)
        self.assertIn("GetGfxFrameBuffer()", game)

    def test_graphics_snapshot(self):
        text = 'extern "C" uint32_t Width() {\n    return 1;\n}\n'
        out = overlap_lus.graphics_snapshot(text, (("Width", "uint32_t", "320"),))
        self.assertIn("if (Shipyard_OverlapIsGameWorker()) return shipyardGraphics.value0;", out)
        self.assertIn("shipyardGraphics.value0 = Width();", out)
        self.assertIn("} shipyardGraphics = { 320 };", out)

    def test_interpreter_locks(self):
        text = ("void Gui::A() {\n    mInterpreter.lock()->x = 1;\n    mInterpreter.lock()->y = 2;\n}\n"
                "void Gui::B() {\n    mInterpreter.lock()->x = 1;\n}\n")
        out = lus_gui.cache_interpreter_locks(text, {"Gui::A": 2})
        self.assertEqual(out.count("mInterpreter.lock()"), 2)
        self.assertIn("    auto xboxInterpreter = mInterpreter.lock();\n    xboxInterpreter->x = 1;", out)
        with self.assertRaises(ValueError):
            lus_gui.cache_interpreter_locks(text, {})

    def test_projected_program_runs(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        if not vswhere.is_file():
            self.skipTest('host MSVC unavailable')
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-passive-gui-') as folder:
            output = Path(folder)
            (output / 'gui.cpp').write_text(projected_program(), encoding='utf-8')
            batch = output / 'build.cmd'
            batch.write_text('@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                             'cl /nologo /std:c++17 /EHsc /W4 /WX /wd4100 gui.cpp /Fegui.exe\n'
                             'exit /b %errorlevel%\n')
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)], cwd=output,
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(output / 'gui.exe')], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('passive gui: intents deferred', result.stdout)


if __name__ == '__main__':
    unittest.main()
