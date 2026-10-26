// GUI construction runs with game and synthesis idle; replay owns these clones.
// The render owner pins textures and retires them through the backend adapter.
// Unknown callbacks and multiple viewports require synchronous GUI rendering.
#ifndef SHIPYARD_OVERLAP_GUI_FRAME_CACHE_H
#define SHIPYARD_OVERLAP_GUI_FRAME_CACHE_H
#include "imgui.h"
#include <vector>

namespace Shipyard {
namespace Overlap {
struct GuiTextureLifetime {
    typedef void (*Operation)(void*, ImTextureID);
    void* context;
    Operation retain, retire;
    GuiTextureLifetime() : context(NULL), retain(NULL), retire(NULL) {}
    GuiTextureLifetime(void* value, Operation pin, Operation release)
        : context(value), retain(pin), retire(release) {}
};

class GuiFrameCache {
public:
    GuiFrameCache() {}
    ~GuiFrameCache() { reset(); }

    bool capture(const ImDrawData& source, ImTextureID gameMarker,
                 const GuiTextureLifetime& lifetime, bool multipleViewports = false) {
        reset();
        if (!source.Valid || !gameMarker || multipleViewports || !lifetime.retain || !lifetime.retire) return false;
        if (source.CmdListsCount != source.CmdLists.Size) return false;
        for (int i = 0; i != source.CmdListsCount; ++i) {
            const ImDrawList* list = source.CmdLists[i];
            if (!list) return false;
            for (int j = 0; j != list->CmdBuffer.Size; ++j) {
                const ImDrawCmd& cmd = list->CmdBuffer[j];
                if (cmd.UserCallback && cmd.UserCallback != ImDrawCallback_ResetRenderState) return false;
            }
        }
        lifetime_ = lifetime;
        try {
            data_.CmdLists.reserve(source.CmdListsCount);
            for (int i = 0; i != source.CmdListsCount; ++i) {
                ImDrawList* list = source.CmdLists[i]->CloneOutput();
                data_.CmdLists.push_back(list);
                for (int j = 0; j != list->CmdBuffer.Size; ++j) {
                    ImDrawCmd& cmd = list->CmdBuffer[j];
                    if (cmd.TextureId == gameMarker && !cmd.UserCallback) {
                        gameSlots_.push_back(GameSlot(i, j, cmd.ElemCount));
                        cmd.TextureId = NULL;
                        cmd.ElemCount = 0;
                    } else if (!cmd.UserCallback) {
                        pin(cmd.TextureId);
                    }
                }
            }
        } catch (...) {
            reset();
            throw;
        }
        data_.CmdListsCount = source.CmdListsCount;
        data_.TotalIdxCount = source.TotalIdxCount;
        data_.TotalVtxCount = source.TotalVtxCount;
        data_.DisplayPos = source.DisplayPos;
        data_.DisplaySize = source.DisplaySize;
        data_.FramebufferScale = source.FramebufferScale;
        data_.OwnerViewport = NULL;
        data_.Valid = true;
        return true;
    }

    void retargetGame(ImTextureID texture) {
        if (!data_.Valid) return;
        if (!gameSlots_.empty()) pin(texture);
        for (size_t i = 0; i != gameSlots_.size(); ++i) {
            const GameSlot& slot = gameSlots_[i];
            ImDrawCmd& cmd = data_.CmdLists[slot.list]->CmdBuffer[slot.command];
            cmd.TextureId = texture;
            cmd.ElemCount = texture ? slot.elements : 0;
        }
    }

    const ImDrawData& drawData() const { return data_; }
    ImDrawData& drawData() { return data_; }

    void reset() {
        for (int i = 0; i != data_.CmdLists.Size; ++i) {
            ImDrawList* list = data_.CmdLists[i];
            list->~ImDrawList();
            ImGui::MemFree(list);
        }
        data_.Clear();
        gameSlots_.clear();
        for (size_t i = 0; i != textures_.size(); ++i) lifetime_.retire(lifetime_.context, textures_[i]);
        textures_.clear();
        lifetime_ = GuiTextureLifetime();
    }

private:
    struct GameSlot {
        int list, command;
        unsigned elements;
        GameSlot(int listIndex, int commandIndex, unsigned count)
            : list(listIndex), command(commandIndex), elements(count) {}
    };
    void pin(ImTextureID texture) {
        if (!texture) return;
        for (size_t i = 0; i != textures_.size(); ++i) if (textures_[i] == texture) return;
        textures_.push_back(texture);
        lifetime_.retain(lifetime_.context, texture);
    }
    GuiFrameCache(const GuiFrameCache&);
    GuiFrameCache& operator=(const GuiFrameCache&);
    ImDrawData data_;
    GuiTextureLifetime lifetime_;
    std::vector<ImTextureID> textures_;
    std::vector<GameSlot> gameSlots_;
};
}
}

#endif
