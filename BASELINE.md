# Shipyard package boundary

The manifest covers production files under `python/` and `sources/`. Its hash
and a Git revision let consumers detect changes to the checkout they selected.

| Package provides | Consumer provides |
| --- | --- |
| Xenos renderer and framebuffer source inputs | Fast3D interfaces, device lifecycle, and application integration |
| Shader lowering and ImGui source inputs | Shader generation, GUI lifecycle, and input policy |
| Native async primitives and source-locked BS projection | Pinned upstream BS header, worker policy, and generated-header staging |
| Vertex-stream transform | Version-specific loop anchors, color expression, namespace, and write counts |
| Xenon C++ project settings | Project generation, SDK configuration, and build validation |
| Checkout and file verification | Lock file, source location, and upstream contract checks |

Keep application behavior, resource formats, save handling, and upstream API
adaptation in the consumer. New package interfaces should be introduced only
with a manifest update, a versioned lock contract, and consumer build checks.
Hardware behavior needs validation in the consuming port.
