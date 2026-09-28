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

SceneRenderer &SceneRenderer::set_skybox(
	Material &skybox
) & {
	m.skybox = &skybox;
	return *this;
}

SceneRenderer &&SceneRenderer::set_skybox(
	Material &skybox
) && {
	m.skybox = &skybox;
	return std::move(*this);
}

void SceneRenderer::reset_models() {
	for(Model *model : m.models)
		model->reset();
}

void SceneRenderer::draw_opaque(
	gfx::CommandList &commands,
	PushConstants &constants
) {
	reset_models();
	for(const Instance &instance : m.instances) {
		if(instance.options.material_override) {
			instance.model->draw(
				commands,
				constants,
				*instance.options.material_override,
				instance.transform
			);
		} else {
			instance.model->draw(commands, constants, instance.transform);
		}
	}
}

void SceneRenderer::draw_cascaded_shadows(
	gfx::CommandList &commands,
	PushConstants &constants,
	gfx::Pipeline &pipeline
) {
	reset_models();
	for(const Instance &instance : m.instances) {
		if(instance.options.casts_shadow)
			instance.model->draw_shadow(commands, constants, pipeline, instance.transform);
	}
}

void SceneRenderer::draw_local_shadows(
	gfx::CommandList &commands,
	PushConstants &constants,
	gfx::Pipeline &pipeline,
	std::span<const LocalShadowView> views
) {
	reset_models();
	for(const Instance &instance : m.instances) {
		if(instance.options.casts_shadow) {
			instance.model->draw_local_shadows(
				commands,
				constants,
				pipeline,
				views,
				instance.transform
			);
		}
	}
}

void SceneRenderer::draw_depth_prepass(
	gfx::CommandList &commands,
	PushConstants &constants,
	gfx::Pipeline &one_sided_pipeline,
	gfx::Pipeline &two_sided_pipeline
) {
	reset_models();
	for(const Instance &instance : m.instances) {
		gfx::Pipeline &pipeline = instance.options.two_sided
			? two_sided_pipeline
			: one_sided_pipeline;
		instance.model->draw_depth_prepass(
			commands,
			constants,
			pipeline,
			instance.transform
		);
	}
}

void SceneRenderer::draw_probe(
	gfx::CommandList &commands,
	PushConstants &constants,
	gfx::Pipeline &pipeline
) {
	reset_models();
	for(const Instance &instance : m.instances) {
		if(!instance.options.captured_by_probes)
			continue;
		if(instance.options.material_override) {
			instance.model->draw_probe(
				commands,
				constants,
				pipeline,
				*instance.options.material_override,
				instance.transform
			);
		} else {
			instance.model->draw_probe(
				commands,
				constants,
				pipeline,
				instance.transform
			);
		}
	}
}

void SceneRenderer::draw_skybox(
	gfx::CommandList &commands,
	PushConstants &constants,
	gfx::Pipeline *pipeline,
	uint32_t instance_count
) {
	if (pipeline)
		m.skybox->bind(commands, constants, *pipeline);
	else
		m.skybox->bind(commands, constants);

	commands.set_root_data(&constants, sizeof(PushConstants));
	commands.draw(3, instance_count);
}
