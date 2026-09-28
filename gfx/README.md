# gfx modules

The Vulkan layer is split into independent xmake targets:

- `gfx`: instance, device, command recording, bindless descriptors, buffers,
  images, textures, and synchronous submission.
- `gfx_image_io`: optional STB-backed image loading.
- `gfx_slang`: runtime Slang compilation and graphics/compute pipelines.
- `gfx_framegraph`: render/compute pass scheduling and resource barriers.
- `gfx_present`: swapchain and frame submission.
- `gfx_sdl`: SDL-to-Vulkan surface creation.
- `gfx_imgui`: Dear ImGui and ImGuizmo.

## Examples

Two standalone examples live in `gfx/examples`:

- `compute.cpp` runs a headless compute dispatch and validates the resulting
  storage buffer.
- `triangle.cpp` opens an SDL window and draws a triangle using dynamic
  rendering without the FrameGraph.

Build them independently with:

```powershell
xmake build gfx_compute_example
xmake build gfx_triangle_example
```

The compute example only links `gfx` and `gfx_slang`. The triangle additionally
links `gfx_present` and `gfx_sdl`. Applications that want scheduled passes can
link `gfx_framegraph` separately.
