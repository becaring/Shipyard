# Shipyard package boundary

The manifest covers package files under `python/`, `sources/` and the selected
`third_party/` inputs. Its hash
and a Git revision let consumers detect changes to the checkout they selected.

| Package provides | Consumer provides |
| --- | --- |
| Xenos renderer, framebuffer, shader cache, shader lowering and pacing | Fast3D interface and device/window lifecycle; optional shader-cache path policy |
| ImGui renderer and physical XInput | Application GUI lifecycle, menu policy and game input mapping |
| Native async, clock, CRT, filesystem, storage device and atomic file writer | Title identity, save serialization, mount lifetime and caller policy |
| XAudio2 sink and PCM pool | Audio producer and allowed channel-layout policy |
| Source-locked BS and JSON projections | Pinned upstream inputs and generated-state staging |
| Fast3D unit-color and vertex-stream helpers | Version-specific interpreter fields and loop anchors |
| Xenon C++ project settings | Project generation, SDK configuration, and build validation |
| Checkout and file verification | Lock file, source location, and upstream contract checks |

Keep application behavior, resource formats, game save serialization/policy,
and upstream API adaptation in the consumer. New package interfaces should be introduced only
with a manifest update, a versioned lock contract, and consumer build checks.
Hardware behavior needs validation in the consuming port.
