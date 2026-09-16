#pragma once

#include <span>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "model.h"

// Persistent scene draw list shared by the depth, shadow, probe, and opaque
// passes. Frame-graph callbacks describe a pass once instead of manually
// repeating every model instance.
class SceneRenderer {
public:
	struct InstanceOptions {
		Material *material_override = nullptr;
		bool two_sided = false;
		bool casts_shadow = true;
		bool captured_by_probes = true;
	};

private:
	struct Instance {
		Model *model = nullptr;
		glm::mat4 transform = glm::mat4(1.0f);
		InstanceOptions options;
	};

	struct M {
		std::vector<Instance> instances;
		std::vector<Model *> models;
	} m;

	explicit SceneRenderer(M m) : m(std::move(m)) {}

	void add_instance(Model &model, const glm::mat4 &transform, InstanceOptions options);
	void reset_models();

public:
	SceneRenderer(SceneRenderer &&) noexcept = default;
	SceneRenderer &operator=(SceneRenderer &&) noexcept = default;
	SceneRenderer(const SceneRenderer &) = delete;
	SceneRenderer &operator=(const SceneRenderer &) = delete;

	static SceneRenderer create() { return SceneRenderer(M{}); }

	SceneRenderer &add(
		Model &model,
		const glm::mat4 &transform = glm::mat4(1.0f),
		InstanceOptions options = {}
	) &;
	SceneRenderer &&add(
		Model &model,
		const glm::mat4 &transform = glm::mat4(1.0f),
		InstanceOptions options = {}
	) &&;

	void draw_opaque(FrameGraph::PassContext &ctx, PushConstants &constants);
	void draw_cascaded_shadows(
		FrameGraph::PassContext &ctx,
		PushConstants &constants,
		Pipeline &pipeline
	);
	void draw_local_shadows(
		FrameGraph::PassContext &ctx,
		PushConstants &constants,
		Pipeline &pipeline,
		std::span<const LocalShadowView> views
	);
	void draw_depth_prepass(
		FrameGraph::PassContext &ctx,
		PushConstants &constants,
		Pipeline &one_sided_pipeline,
		Pipeline &two_sided_pipeline
	);
	void draw_probe(
		FrameGraph::PassContext &ctx,
		PushConstants &constants,
		Pipeline &pipeline
	);
};
