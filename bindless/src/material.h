#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>

#include "framegraph.h"
#include "gpu_buffer.h"
#include "material_param.h"
#include "shader.h"
#include "shader_types.h"

class MaterialRegistry;

// Runtime material data. Loading, caching, named-resource resolution, and
// reload policy live in MaterialRegistry.
class Material {
	struct M {
		Pipeline *pipeline;
		GPUBuffer<uint8_t> buffer;
	} m;

	explicit Material(M m) : m(std::move(m)) {}
	friend class MaterialRegistry;

	static Material create_impl(
		Pipeline &pipeline,
		const MaterialParam *params,
		size_t count
	);

	template<typename Range>
	static Material create(Pipeline &pipeline, const Range &params) {
		return create_impl(pipeline, params.data(), params.size());
	}

	static Material create(
		Pipeline &pipeline,
		std::initializer_list<MaterialParam> params
	) {
		return create_impl(pipeline, params.begin(), params.size());
	}

public:
	Material(Material &&) noexcept = default;
	Material &operator=(Material &&) noexcept = default;
	Material(const Material &) = delete;
	Material &operator=(const Material &) = delete;

	void bind(FrameGraph::PassContext &ctx, PushConstants &push_constants) {
		push_constants.material_handle = m.buffer.handle();
		ctx.bind_pipeline(m.pipeline->pipeline());
	}

	// Bind this material's parameter block with a layout-compatible override
	// pipeline. Probe capture uses this to reuse ordinary PBR materials without
	// recursively sampling the probe it is currently producing.
	void bind(
		FrameGraph::PassContext &ctx,
		PushConstants &push_constants,
		Pipeline &pipeline
	) {
		push_constants.material_handle = m.buffer.handle();
		ctx.bind_pipeline(pipeline.pipeline());
	}
};
