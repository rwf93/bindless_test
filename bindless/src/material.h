#pragma once

#include <vulkan/vulkan.h>
#include <filesystem>
#include <unordered_map>
#include <string>
#include <initializer_list>

#include "vkcontext.h"
#include "shader.h"
#include "resource.h"

// A standalone material object. Owns its own MaterialBuffer (1-slot) and a
// pipeline reference. `bind()` sets material_handle + material index + binds
// the pipeline — so a draw call only needs the geometry + a Material.
class Material {
	struct M {
		PipelineInfo *pipeline = nullptr;
		MaterialBuffer buffer;
		uint32_t index = 0;
	} m;

	explicit Material(M m) : m(std::move(m)) {}

	// Shared body. Both the template (for std::vector<MaterialParam> etc.) and
	// the initializer_list overload (for braced-init-lists at call sites)
	// forward to this. A braced-init-list can only deduce to an explicit
	// std::initializer_list<T> parameter, not a generic Range, so the
	// overload is required for `Material::create(p, {...})` syntax.
	static Material create_impl(PipelineInfo &pipeline, const MaterialParam *params, size_t count);
public:
	Material() = default;
	Material(Material &&) noexcept = default;
	Material &operator=(Material &&) noexcept = default;
	Material(const Material &) = delete;
	Material &operator=(const Material &) = delete;

	template<typename Range>
	static Material create(PipelineInfo &pipeline, const Range &params) {
		return create_impl(pipeline, params.data(), params.size());
	}
	static Material create(PipelineInfo &pipeline, std::initializer_list<MaterialParam> params) {
		return create_impl(pipeline, params.begin(), params.size());
	}

	// Load a single material from a TOML file. One TOML = one Material.
	// `path` is a VFS virtual path (e.g. "materials/helmet_pbr.toml") —
	// resolved against `vfs` internally. Absolute paths pass through.
	// The `pipeline` key selects the shader. String values are texture file
	// paths (resolved via the same `vfs`); doubles are raw floats; arrays
	// are raw float arrays.
	// Cached by virtual path — loading the same path twice returns the same Material.
	static std::unordered_map<std::string, Material> material_cache;

	static Material &load_toml(VFS &vfs, const std::string &path);

	void bind(VkCommandBuffer cmd, PushConstants &push_constants) {
		push_constants.material_handle = m.buffer.handle();
		push_constants.material = m.index;
		device().cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m.pipeline->pipeline);
	}
};
