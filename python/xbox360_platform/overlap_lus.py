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


def resource_manager(text):
    """Retain every returned owner; cache mutation and unload wait for replay."""
    # Rewrite returns before adding the helper, whose own return must stay plain.
    text, count = re.subn(r"return (resource|cachedResource);",
                          r"return Shipyard_OverlapKeepResource(\1);", text)
    if not count:
        raise ValueError("Resource return drift")
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
