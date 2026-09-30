#pragma once
#include "imgui_internal.h"
#include <vector>

namespace Shipyard {
namespace ImGuiPlatform {
// poll: zero while pending, positive on acceptance, negative on cancel/error.
struct KeyboardTransport {
    bool (*start)(unsigned, bool, bool, const wchar_t*, wchar_t*, unsigned);
    int (*poll)();
    void (*cancel)();
};

class Keyboard {
    ImGuiID requested;
    bool pending;
    bool valid;
    std::vector<wchar_t> initial;
    std::vector<wchar_t> result;
public:
    Keyboard() : requested(0), pending(false), valid(false) {}
    void Reset(const KeyboardTransport& transport) {
        if (pending) transport.cancel();
        requested = 0;
        pending = false;
        valid = false;
    }
    bool Update(const KeyboardTransport& transport, unsigned slot, bool visible) {
        ImGuiContext& g = *GImGui;
        ImGuiInputTextState* state = ImGui::GetInputTextState(g.ActiveId);
        const bool wants = visible && g.IO.WantTextInput && state &&
                           !(state->Flags & ImGuiInputTextFlags_ReadOnly);
        if (pending) {
            // Once focus/menu is lost, never apply this result even if focus returns.
            valid = valid && wants && state->ID == requested;
            const int completion = transport.poll();
            if (!completion) return true;
            pending = false;
            if (completion > 0 && valid) {
                // Queue characters through InputText's own filters/callbacks and capacity checks.
                // ClearText also permits accepting an empty replacement.
                state->ClearText();
                state->ClearSelection();
                for (unsigned i = 0; i < result.size() && result[i]; ++i) {
                    unsigned c = static_cast<unsigned>(result[i]);
                    if (c >= 0xd800 && c <= 0xdbff && i + 1 < result.size() &&
                        result[i + 1] >= 0xdc00 && result[i + 1] <= 0xdfff) {
                        c = 0x10000 + ((c - 0xd800) << 10) + (result[++i] - 0xdc00);
                    } else if (c >= 0xd800 && c <= 0xdfff) c = 0xfffd;
                    g.IO.AddInputCharacter(c);
                }
            }
            return true; // Consume the completion frame's controller buttons too.
        }
        if (!wants) { requested = 0; return false; }
        if (requested == state->ID || slot >= 4) return false;
        requested = state->ID;
        initial.clear();
        const char* cursor = state->TextA.Data;
        while (*cursor) {
            unsigned c;
            cursor += ImTextCharFromUtf8(&c, cursor, NULL);
            if (c > 0xffff) {
                initial.push_back(static_cast<wchar_t>(0xd800 + ((c - 0x10000) >> 10)));
                initial.push_back(static_cast<wchar_t>(0xdc00 + ((c - 0x10000) & 0x3ff)));
            } else initial.push_back(static_cast<wchar_t>(c));
        }
        initial.push_back(0);
        result.assign(ImMax(state->BufCapacity, 2), 0);
        const bool numeric = (state->Flags & (ImGuiInputTextFlags_CharsDecimal |
                                            ImGuiInputTextFlags_CharsScientific)) != 0;
        const bool password = (state->Flags & ImGuiInputTextFlags_Password) != 0;
        pending = transport.start(slot, numeric, password, &initial[0], &result[0],
                                  static_cast<unsigned>(result.size()));
        valid = pending;
        return pending;
    }
};
} // namespace ImGuiPlatform
} // namespace Shipyard
