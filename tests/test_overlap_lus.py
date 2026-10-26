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

    def test_game_texture_clears_run_on_render(self):
        text = overlap_lus.texture_cache_clear("void Interpreter::TextureCacheClear() {\n    mCache.clear();\n}\n")
        self.assertLess(text.index("Shipyard_OverlapRunOnRender"), text.index("mCache.clear();"))


if __name__ == "__main__":
    unittest.main()
