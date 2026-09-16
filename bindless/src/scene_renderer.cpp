#include "scene_renderer.h"

#include <algorithm>

void SceneRenderer::add_instance(
	Model &model,
	const glm::mat4 &transform,
	InstanceOptions options
) {
	if(std::find(m.models.begin(), m.models.end(), &model) == m.models.end())
		m.models.push_back(&model);
	m.instances.push_back(Instance{
		.model = &model,
		.transform = transform,
		.options = options,
	});
}

SceneRenderer &SceneRenderer::add(
	Model &model,
	const glm::mat4 &transform,
	InstanceOptions options
) & {
	add_instance(model, transform, options);
	return *this;
}

SceneRenderer &&SceneRenderer::add(
	Model &model,
	const glm::mat4 &transform,
	InstanceOptions options
) && {
	add_instance(model, transform, options);
	return std::move(*this);
}

void SceneRenderer::reset_models() {
	for(Model *model : m.models)
		model->reset();
}

void SceneRenderer::draw_opaque(
	FrameGraph::PassContext &ctx,
	PushConstants &constants
) {
	reset_models();
	for(const Instance &instance : m.instances) {
		if(instance.options.material_override) {
			instance.model->draw(
				ctx,
				constants,
				*instance.options.material_override,
				instance.transform
			);
		} else {
			instance.model->draw(ctx, constants, instance.transform);
		}
	}
}

void SceneRenderer::draw_cascaded_shadows(
	FrameGraph::PassContext &ctx,
	PushConstants &constants,
	Pipeline &pipeline
) {
	reset_models();
	for(const Instance &instance : m.instances) {
		if(instance.options.casts_shadow)
			instance.model->draw_shadow(ctx, constants, pipeline, instance.transform);
	}
}

void SceneRenderer::draw_local_shadows(
	FrameGraph::PassContext &ctx,
	PushConstants &constants,
	Pipeline &pipeline,
	std::span<const LocalShadowView> views
) {
	reset_models();
	for(const Instance &instance : m.instances) {
		if(instance.options.casts_shadow) {
			instance.model->draw_local_shadows(
				ctx,
				constants,
				pipeline,
				views,
				instance.transform
			);
		}
	}
}

void SceneRenderer::draw_depth_prepass(
	FrameGraph::PassContext &ctx,
	PushConstants &constants,
	Pipeline &one_sided_pipeline,
	Pipeline &two_sided_pipeline
) {
	reset_models();
	for(const Instance &instance : m.instances) {
		Pipeline &pipeline = instance.options.two_sided
			? two_sided_pipeline
			: one_sided_pipeline;
		instance.model->draw_depth_prepass(
			ctx,
			constants,
			pipeline,
			instance.transform
		);
	}
}

void SceneRenderer::draw_probe(
	FrameGraph::PassContext &ctx,
	PushConstants &constants,
	Pipeline &pipeline
) {
	reset_models();
	for(const Instance &instance : m.instances) {
		if(!instance.options.captured_by_probes)
			continue;
		if(instance.options.material_override) {
			instance.model->draw_probe(
				ctx,
				constants,
				pipeline,
				*instance.options.material_override,
				instance.transform
			);
		} else {
			instance.model->draw_probe(
				ctx,
				constants,
				pipeline,
				instance.transform
			);
		}
	}
}
