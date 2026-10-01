#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>

#include "gfx/buffer.h"
#include "gfx/command_list.h"
#include "material_param.h"
#include "material_layout.h"
#include "gfx/slang/shader.h"
#include "shader_types.h"

class MaterialRegistry;

// Runtime material data. Loading, caching, named-resource resolution, and
// reload policy live in MaterialRegistry.
class Material {
	struct M {
		gfx::Pipeline *pipeline;
		gfx::Buffer<uint8_t> buffer;
	} m;

	explicit Material(M m) : m(std::move(m)) {}
	friend class MaterialRegistry;

	static Material create_impl(
		gfx::UploadBatch &upload,
		gfx::Pipeline &pipeline,
		const MaterialLayout &layout,
		const MaterialParam *params,
		size_t count
	);

	template<typename Range>
	static Material create(
		gfx::Pipeline &pipeline,
		const MaterialLayout &layout,
		const Range &params
	) {
		auto upload = gfx::UploadBatch::create(pipeline.device());
		auto result = create_impl(upload, pipeline, layout, params.data(), params.size());
		upload.submit().wait();
		return result;
	}

	template<typename Range>
	static Material create(
		gfx::UploadBatch &upload,
		gfx::Pipeline &pipeline,
		const MaterialLayout &layout,
		const Range &params
	) {
		return create_impl(upload, pipeline, layout, params.data(), params.size());
	}

	static Material create(
		gfx::Pipeline &pipeline,
		const MaterialLayout &layout,
		std::initializer_list<MaterialParam> params
	) {
		auto upload = gfx::UploadBatch::create(pipeline.device());
		auto result = create_impl(upload, pipeline, layout, params.begin(), params.size());
		upload.submit().wait();
		return result;
	}

public:
	Material(Material &&) noexcept = default;
	Material &operator=(Material &&) noexcept = default;
	Material(const Material &) = delete;
	Material &operator=(const Material &) = delete;

	void bind(gfx::CommandList &commands, PushConstants &push_constants) {
		push_constants.material_handle = m.buffer.handle();
		commands.set_pipeline(*m.pipeline);
	}

	// Bind this material's parameter block with a layout-compatible override
	// pipeline. Probe capture uses this to reuse ordinary PBR materials without
	// recursively sampling the probe it is currently producing.
	void bind(
		gfx::CommandList &commands,
		PushConstants &push_constants,
		gfx::Pipeline &pipeline
	) {
		push_constants.material_handle = m.buffer.handle();
		commands.set_pipeline(pipeline);
	}
};
