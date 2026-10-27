import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from xbox360_platform import overlap_lus

KEY = "mVariables.find(Shipyard::ConsoleVariables::NameKey(name, Shipyard::ConsoleVariables::NameKey::Borrow))"
CVARS = "namespace Ship {\nstd::shared_ptr<CVar> ConsoleVariable::Get(const char* name) {\n    auto it = " + KEY + ";\n"
CVARS += "    return it != mVariables.end() ? it->second : nullptr;\n}\n"
for getter in ("GetInteger", "GetFloat", "GetString", "GetColor", "GetColor24"):
    CVARS += "int ConsoleVariable::" + getter + "(const char* name) {\n    auto found = " + KEY + ";\n"
    CVARS += "    return found != mVariables.end();\n}\n"
CVARS += "void ConsoleVariable::SetInteger(const char* name, int value) {\n    auto it = mVariables.find(name);\n"
CVARS += "    if (it != mVariables.end()) it->second->Integer = value;\n}\n}\n"

RESOURCES = """\
std::shared_ptr<IResource> ResourceManager::LoadResourceProcess(const ResourceIdentifier& identifier, bool loadExact) {
    auto cachedResource = GetCachedResource(cacheLine);
    if (cachedResource != nullptr) {
        return cachedResource;
    }
    return resource;
}
std::shared_ptr<IResource> ResourceManager::LoadResource(const ResourceIdentifier& identifier, bool loadExact) {
    auto resource = LoadResourceAsync(identifier, loadExact).get();
    return resource;
}
std::shared_ptr<IResource> ResourceManager::LoadResource(const std::string& path) {
    return resource;
}
size_t ResourceManager::UnloadResource(const ResourceIdentifier& identifier) {
    return 0;
}
void ResourceManager::DirtyResources(const ResourceFilter& filter) {
    Increment();
    mThreadPool->submit_task([this, filter]() -> void {
        Work(filter);
    });
}
void ResourceManager::UnloadResourcesAsync(const ResourceFilter& filter, BS::priority_t priority) {
    mThreadPool->submit_task([this, filter]() -> void { UnloadResourcesProcess(filter); }, priority);
}
"""


class OverlapLusTests(unittest.TestCase):
    def test_only_getters_read_the_render_snapshot(self):
        text = overlap_lus.console_variable_source(CVARS)
        readers, setters = text.split("void ConsoleVariable::SetInteger", 1)
        self.assertEqual(readers.count("const Snapshot& variables = ReadVariables();"), 6)
        self.assertNotIn("ReadVariables", setters)
        self.assertIn("cursor != mVariables.end()", text)

    def test_resource_owners_follow_packets_and_mutation_waits(self):
        text = overlap_lus.resource_manager(RESOURCES)
        self.assertIn("return Shipyard_OverlapKeepResource(resource);", text)
        helper = text[text.index("Shipyard_OverlapKeepResource("):text.index("\n}\n")]
        self.assertNotIn("return Shipyard_OverlapKeepResource", helper)
        self.assertIn("UnloadResource(const ResourceIdentifier& identifier) {\n    Shipyard_OverlapFence();", text)
        dirty = text[text.index("void ResourceManager::DirtyResources"):text.index("void ResourceManager::UnloadResourcesAsync")]
        self.assertLess(dirty.index("Shipyard_OverlapFence();"), dirty.index("mThreadPool->submit_task"))
        self.assertEqual(dirty.count("Work(filter);"), 2)
        self.assertIn("        UnloadResourcesProcess(filter);\n    } else {", text)

    def test_forwarded_owners_are_retained_once(self):
        def body(text, signature):
            start = text.index(signature)
            return text[start:text.index("\n}\n", start)]
        text = overlap_lus.resource_manager(RESOURCES)
        process = body(text, "ResourceManager::LoadResourceProcess(const ResourceIdentifier&")
        self.assertIn("        return cachedResource;", process)
        self.assertIn("return Shipyard_OverlapKeepResource(resource);", process)
        # A pool worker retains nothing, so the future's result is still retained.
        load = body(text, "ResourceManager::LoadResource(const ResourceIdentifier&")
        self.assertIn("return Shipyard_OverlapKeepResource(resource);", load)
        inline = RESOURCES.replace("LoadResourceAsync(identifier, loadExact).get()",
                                   "LoadResourceProcess(identifier, loadExact)")
        load = body(overlap_lus.resource_manager(inline), "ResourceManager::LoadResource(const ResourceIdentifier&")
        self.assertIn("    return resource;", load)
        self.assertNotIn("KeepResource", load)
        with self.assertRaises(ValueError):
            overlap_lus.resource_manager(RESOURCES.replace("return cachedResource;", "return nullptr;"))

    def test_game_texture_clears_run_on_render(self):
        text = overlap_lus.texture_cache_clear("void Interpreter::TextureCacheClear() {\n    mCache.clear();\n}\n")
        self.assertLess(text.index("Shipyard_OverlapRunOnRender"), text.index("mCache.clear();"))

    def test_game_input_blocks_read_the_handoff_snapshot(self):
        deck = "".join("bool ControlDeck::%sGameInputBlocked() {\n    return ImGui::GetCurrentContext() != 0;\n}\n" % name
                       for name in ("Gamepad", "Keyboard", "Mouse"))
        text = overlap_lus.control_deck(deck)
        for kind, name in enumerate(("Gamepad", "Keyboard", "Mouse")):
            body = text[text.index(name + "GameInputBlocked() {"):]
            self.assertLess(body.index("Shipyard_OverlapInputBlocked(%d, &blocked)" % kind), body.index("ImGui::"))
        with self.assertRaises(ValueError):
            overlap_lus.control_deck(deck.replace("MouseGameInputBlocked", "MouseBlocked"))


if __name__ == "__main__":
    unittest.main()
