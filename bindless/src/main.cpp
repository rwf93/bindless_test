#include <spdlog/spdlog.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <glm/glm.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include "camera.h"
#include "vktools.h"
#include "vkinfo.h"
#include "vkcontext.h"
#include "framegraph.h"
#include "gpu_buffer.h"
#include "light_probe.h"
#include "local_shadow.h"
#include "material_registry.h"
#include "model.h"
#include "gltf_loader.h"
#include "pipeline_registry.h"
#include "scene_renderer.h"
#include "shader_types.h"
#include "texture_registry.h"
#include "vfs.h"
#include "cascaded_shadow.h"

static SDL_Event event;
static uint32_t image_index;
static bool quit = false;
static constexpr uint32_t APP_WIDTH = 1600;
static constexpr uint32_t APP_HEIGHT = 900;
static constexpr float CAMERA_FOV = glm::radians(70.0f);
static constexpr float CAMERA_NEAR = 0.1f;
static constexpr float CAMERA_FAR = 5000.0f;
static constexpr uint32_t LIGHT_COUNT = 4;
static constexpr size_t LIGHT_PROBE_CAPACITY = 8;
static_assert(
	LIGHT_COUNT * LOCAL_SHADOW_FACE_COUNT <= LOCAL_SHADOW_MAX_VIEW_COUNT,
	"Local shadow atlas layers exceed the per-object compact view list"
);

struct EditableLight {
	LightType type;
	glm::vec3 position;
	glm::vec3 direction;
	glm::vec3 color;
	float intensity;
	float range;
	float inner_cone_degrees;
	float outer_cone_degrees;
	bool casts_shadow = true;
	float shadow_bias = 0.0001f;
};

static bool draw_lighting_widget(
	glm::vec3 &shadow_direction,
	glm::vec3 &shadow_color,
	float &shadow_intensity,
	std::array<EditableLight, LIGHT_COUNT> &lights
) {
	bool changed = false;
	if(!ImGui::Begin("Lighting")) {
		ImGui::End();
		return false;
	}

	if(ImGui::CollapsingHeader("Cascaded shadow", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextDisabled("Direction points from the scene toward the light");
		glm::vec3 edited_direction = shadow_direction;
		if(ImGui::DragFloat3(
			"Direction",
			&edited_direction.x,
			0.01f,
			-1.0f,
			1.0f,
			"%.3f",
			ImGuiSliderFlags_AlwaysClamp
		)) {
			const float length_squared = glm::dot(edited_direction, edited_direction);
			if(length_squared > 0.000001f) {
				shadow_direction = edited_direction / glm::sqrt(length_squared);
				changed = true;
			}
		}
		changed |= ImGui::ColorEdit3("Color", &shadow_color.x, ImGuiColorEditFlags_Float);
		changed |= ImGui::DragFloat("Intensity", &shadow_intensity, 0.05f, 0.0f, 100.0f, "%.2f");
	}

	if(ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
		for(uint32_t i = 0; i < LIGHT_COUNT; i++) {
			auto &light = lights[i];
			ImGui::PushID(int(i));
			ImGui::Separator();
			ImGui::Text("Light %u", i);

			int type = int(light.type);
			if(ImGui::Combo("Type", &type, "Point\0Spot\0")) {
				light.type = LightType(type);
				changed = true;
			}

			changed |= ImGui::DragFloat3("Position", &light.position.x, 0.1f, -1000.0f, 1000.0f, "%.2f");
			changed |= ImGui::ColorEdit3("Color", &light.color.x, ImGuiColorEditFlags_Float);
			changed |= ImGui::DragFloat("Intensity", &light.intensity, 1.0f, 0.0f, 5000.0f, "%.1f");
			changed |= ImGui::DragFloat("Range", &light.range, 0.25f, 0.0f, 1000.0f, "%.1f");
			ImGui::TextDisabled("Range 0 disables the smooth distance cutoff");
			changed |= ImGui::Checkbox("Casts shadow", &light.casts_shadow);
			if(light.casts_shadow)
				changed |= ImGui::DragFloat("Shadow bias", &light.shadow_bias, 0.000005f, 0.0f, 0.005f, "%.6f");

			if(light.type == LightType::Spot) {
				glm::vec3 edited_direction = light.direction;
				if(ImGui::DragFloat3(
					"Direction",
					&edited_direction.x,
					0.01f,
					-1.0f,
					1.0f,
					"%.3f",
					ImGuiSliderFlags_AlwaysClamp
				)) {
					const float length_squared = glm::dot(edited_direction, edited_direction);
					if(length_squared > 0.000001f) {
						light.direction = edited_direction / glm::sqrt(length_squared);
						changed = true;
					}
				}

				changed |= ImGui::DragFloat(
					"Inner cone",
					&light.inner_cone_degrees,
					0.25f,
					0.0f,
					89.0f,
					"%.1f deg",
					ImGuiSliderFlags_AlwaysClamp
				);
				changed |= ImGui::DragFloat(
					"Outer cone",
					&light.outer_cone_degrees,
					0.25f,
					0.1f,
					89.9f,
					"%.1f deg",
					ImGuiSliderFlags_AlwaysClamp
				);
				light.inner_cone_degrees = glm::min(
					light.inner_cone_degrees,
					light.outer_cone_degrees
				);
			}
			ImGui::PopID();
		}
	}

	ImGui::End();
	return changed;
}

static void draw_light_probe_widget(LightProbeSet &probe_set) {
	if(!ImGui::Begin("Environment probes")) {
		ImGui::End();
		return;
	}

	if(ImGui::Button("Refresh all"))
		probe_set.mark_all_dirty();
	ImGui::SameLine();
	if(ImGui::Button("Rebuild BRDF LUT"))
		probe_set.invalidate_brdf_lut();

	for(size_t index = 0; index < probe_set.size(); index++) {
		auto &probe = probe_set[index];
		ImGui::PushID(int(index));
		if(ImGui::CollapsingHeader(
			probe.name().c_str(),
			index == 0 ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None
		)) {
			bool global = probe.global();
			if(ImGui::Checkbox("Global fallback", &global))
				probe.set_global(global);

			glm::vec3 position = probe.position();
			if(ImGui::DragFloat3(
				"Position",
				&position.x,
				0.1f,
				-1000.0f,
				1000.0f,
				"%.2f"
			)) {
				probe.set_position(position);
			}

			if(!global) {
				glm::vec3 box_minimum = probe.box_min();
				glm::vec3 box_maximum = probe.box_max();
				const bool minimum_changed = ImGui::DragFloat3(
					"Box minimum",
					&box_minimum.x,
					0.25f,
					-2000.0f,
					2000.0f,
					"%.2f"
				);
				const bool maximum_changed = ImGui::DragFloat3(
					"Box maximum",
					&box_maximum.x,
					0.25f,
					-2000.0f,
					2000.0f,
					"%.2f"
				);
				if(minimum_changed || maximum_changed)
					probe.set_box(box_minimum, box_maximum);
			}

			float intensity = probe.intensity();
			if(ImGui::DragFloat(
				"Intensity",
				&intensity,
				0.02f,
				0.0f,
				10.0f,
				"%.2f"
			)) {
				probe.set_intensity(intensity);
			}

			float priority = probe.priority();
			if(ImGui::DragFloat(
				"Priority",
				&priority,
				0.02f,
				0.0f,
				10.0f,
				"%.2f"
			)) {
				probe.set_priority(priority);
			}

			bool live_update = probe.live_update();
			if(ImGui::Checkbox("Capture every frame", &live_update))
				probe.set_live_update(live_update);
			ImGui::SameLine();
			if(ImGui::Button("Refresh"))
				probe.mark_dirty();

			ImGui::TextDisabled(
				"%u px, %u specular mips, %s",
				probe.capture().resolution(),
				probe.specular().mip_count(),
				probe.needs_capture() ? "capture pending" : "captured"
			);
		}
		ImGui::PopID();
	}
	ImGui::End();
}

static void set_viewport_and_scissor(VkCommandBuffer command, uint32_t width, uint32_t height) {
	VkRect2D scissor = {{0, 0}, {width, height}};
	VkViewport viewport = {0.0f, 0.0f, float(width), float(height), 0.0f, 1.0f};
	device().cmdSetScissor(command, 0, 1, &scissor);
	device().cmdSetViewport(command, 0, 1, &viewport);
}

// Convenience overload that uses the current frame's command buffer.
void transition(VkImage image, VkImageLayout current_layout, VkImageLayout new_layout) {
	transition(
		vkctx.frames[frame_index].buffer,
		image,
		current_layout,
		new_layout,
		VK_IMAGE_ASPECT_COLOR_BIT
	);
}

int main(int argc, char** argv) {
	if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
		spdlog::error("Couldn't initalize SDL");
	}

	window = SDL_CreateWindow("fuck", APP_WIDTH, APP_HEIGHT, SDL_WINDOW_VULKAN);
	SDL_ShowWindow(window);

	create_vk_shit();
	create_imgui_shit();

	auto vfs = VFS::create("../../vfs.toml");
	auto textures = TextureRegistry::create();
	auto pipelines = PipelineRegistry::create(vfs);
	auto materials = MaterialRegistry::create(vfs, pipelines, textures);

	textures.create<Texture2D>(128, 128, VK_FORMAT_R8G8B8A8_UNORM, [](int x, int y) {
		return ((x / 16) + (y / 16)) % 2 == 0 ? 0x00ff0090u : 0x00000000u;
	}, "missing");

	textures.create<Texture2D>( 1, 1, VK_FORMAT_R8G8B8A8_UNORM, [](int, int) {
		return 0xff000000u;
	}, "black");

	textures.create<Texture2D>(1, 1, VK_FORMAT_R8G8B8A8_UNORM, [](int, int) {
		return 0xffffffffu;
	}, "white");

	auto &color = textures.create<Texture2D>(
		APP_WIDTH, APP_HEIGHT,
		VK_FORMAT_R16G16B16A16_SFLOAT,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		"gbuffer_albedo"
	);

	auto &depth = textures.create<Texture2D>(
		APP_WIDTH, APP_HEIGHT,
		VK_FORMAT_D32_SFLOAT,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		"depth"
	);

	auto &cascaded_shadowmap = textures.create<Texture2DArray>(
		SHADOW_MAP_RESOLUTION,
		SHADOW_MAP_RESOLUTION,
		SHADOW_CASCADE_COUNT,
		VK_FORMAT_D16_UNORM,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		"cascaded_shadowmap"
	);

	auto &local_shadowmap = textures.create<Texture2DArray>(
		LOCAL_SHADOW_MAP_RESOLUTION,
		LOCAL_SHADOW_MAP_RESOLUTION,
		LIGHT_COUNT * LOCAL_SHADOW_FACE_COUNT,
		VK_FORMAT_D32_SFLOAT,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		"local_shadowmap"
	);

	auto light_probes = LightProbeSet::create(
		textures,
		"scene_probes",
		LIGHT_PROBE_CAPACITY
	);

	auto &global_probe = light_probes.add("scene_probe_global");
	global_probe.set_position(glm::vec3(0.0f, 4.0f, 0.0f));
	global_probe.set_global(true);

	auto camera = Camera::create();

	glm::vec3 shadow_light_direction = glm::normalize(glm::vec3(-0.6f, 1.0f, 0.35f));
	glm::vec3 shadow_light_color = glm::vec3(1.0f, 0.96f, 0.9f);
	float shadow_light_intensity = 5.0f / 3.0f;
	std::array<EditableLight, LIGHT_COUNT> editable_lights = {{
		{LightType::Point, glm::vec3(0, 5, 0), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f), 100.0f, 50.0f, 20.0f, 30.0f},
		{LightType::Point, glm::vec3( 10.0f,  10.0f, 10.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f), 100.0f, 50.0f, 20.0f, 30.0f},
		{LightType::Point, glm::vec3(-10.0f, -10.0f, 10.0f), glm::vec3(0.0f,  1.0f, 0.0f), glm::vec3(1.0f), 100.0f, 50.0f, 20.0f, 30.0f},
		{LightType::Spot,  glm::vec3(  0.0f,  10.0f, 10.0f), glm::normalize(glm::vec3(0.0f, -10.0f, -10.0f)), glm::vec3(1.0f), 100.0f, 50.0f, 20.0f, 30.0f},
	}};

	/* Scene specific info */
	auto scene = MultiBuffer<SceneData>::create();
	auto lights = MultiBuffer<LightData>::create(LIGHT_COUNT);
	auto local_shadow_matrices = MultiBuffer<LocalShadowMatrixData>::create(LIGHT_COUNT * LOCAL_SHADOW_FACE_COUNT);

	auto &missing_unlit = materials.load("materials/missing_unlit.toml");
	auto &white_unlit = materials.load("materials/white_unlit.toml");
	auto &white_pbr = materials.load("materials/white_pbr.toml");
	auto &swapchain_write_material = materials.load("materials/swapchain_write.toml");
	auto &skybox_space = materials.load("materials/skybox_space.toml");
	auto &skybox_clouds = materials.load("materials/skybox_clouds.toml");

	auto helmet = GLTFLoader::load_toml(vfs, materials, "generated/helmet/DamagedHelmet.model.toml");
	auto avocado = GLTFLoader::load_toml(vfs, materials, "generated/avocado/Avocado.model.toml");
	auto sponza = GLTFLoader::load_toml(vfs, materials, "generated/sponza/Sponza.model.toml");
	//auto flight = GLTFLoader::load_toml(vfs, materials, "generated/flight_helmet/FlightHelmet.model.toml");

	auto scene_renderer = SceneRenderer::create()
		.add(helmet, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f)))
		.add(sponza, glm::mat4(1.0f), {.two_sided = true})
	//	.add(flight,  glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f)))
		.add(avocado, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 5.0f, 0.0f)));

	std::vector<LocalShadowView> local_shadow_views;
	local_shadow_views.reserve(LIGHT_COUNT * LOCAL_SHADOW_FACE_COUNT);

	auto swapchain_target = FrameGraph::ExternalImage::create(
		"swapchain",
		FrameGraph::ImageDesc{
			.format = vkctx.swapchain.image_format,
			.extent = {APP_WIDTH, APP_HEIGHT, 1},
		},
		[&] {
			return ImageRef{
				.image = vkctx.swapchain_images[image_index],
				.view = vkctx.swapchain_views[image_index],
			};
		}
	);

	auto framegraph = FrameGraph::create()
		.add_render_pass("shadow",
			[&](FrameGraph::RenderPassBuilder &pass) {
				pass.read(scene);
				pass.clear_depth(cascaded_shadowmap);
			},
			[&](FrameGraph::PassContext &ctx) {
				set_viewport_and_scissor(ctx, SHADOW_MAP_RESOLUTION, SHADOW_MAP_RESOLUTION);

				PushConstants push_constants = {};
				push_constants.scene_handle = scene.handle();
				auto &shadow_pipeline = pipelines.at("shadow");
				scene_renderer.draw_cascaded_shadows(
					ctx,
					push_constants,
					shadow_pipeline
				);
			}
		)
		.add_render_pass("local_shadow",
			[&](FrameGraph::RenderPassBuilder &pass) {
				pass.read(scene);
				pass.read(local_shadow_matrices);
				pass.clear_depth(local_shadowmap);
			},
			[&](FrameGraph::PassContext &ctx) {
				set_viewport_and_scissor(
					ctx,
					LOCAL_SHADOW_MAP_RESOLUTION,
					LOCAL_SHADOW_MAP_RESOLUTION
				);

				PushConstants push_constants = {};
				push_constants.scene_handle = scene.handle();
				auto &local_shadow_pipeline = pipelines.at("local_shadow");
				scene_renderer.draw_local_shadows(
					ctx,
					push_constants,
					local_shadow_pipeline,
					local_shadow_views
				);
			}
		);

	for(LightProbe &probe : light_probes.probes()) {
		LightProbe *current_probe = &probe;
		const std::string pass_suffix = "_" + probe.name();

		framegraph.add_render_pass("ibl_probe_capture" + pass_suffix,
			[&, current_probe](FrameGraph::RenderPassBuilder &pass) {
				pass.enabled_if([&, current_probe] {
					return light_probes.should_capture(*current_probe);
				});
				pass.read(scene);
				pass.read(lights);
				pass.read(local_shadow_matrices);
				pass.read(*current_probe);
				pass.read(cascaded_shadowmap);
				pass.read(local_shadowmap);
				pass.clear_color(
					current_probe->capture(),
					{0.0f, 0.0f, 0.0f, 1.0f}
				);
				pass.clear_depth(current_probe->depth());
			},
			[&, current_probe](FrameGraph::PassContext &ctx) {
				const uint32_t resolution = current_probe->capture().resolution();
				set_viewport_and_scissor(ctx, resolution, resolution);

				PushConstants push_constants = {};
				push_constants.scene_handle = scene.handle();
				push_constants.light_handle = lights.handle();
				push_constants.probe_handle = current_probe->handle();

				skybox_clouds.bind(
					ctx,
					push_constants,
					pipelines.at("ibl_probe_sky")
				);
				ctx.push_constants(
					VK_SHADER_STAGE_ALL,
					&push_constants,
					sizeof(PushConstants)
				);
				ctx.draw(3, LIGHT_PROBE_FACE_COUNT);

				scene_renderer.draw_probe(
					ctx,
					push_constants,
					pipelines.at("ibl_probe_capture")
				);
			}
		)
		.add_compute_pass("ibl_specular_prefilter" + pass_suffix,
			[&, current_probe](FrameGraph::ComputePassBuilder &pass) {
				pass.enabled_if([&, current_probe] {
					return light_probes.should_capture(*current_probe);
				});
				pass.read(current_probe->capture());
				pass.write(current_probe->specular());
			},
			[&, current_probe](FrameGraph::PassContext &ctx) {
				auto &specular = current_probe->specular();
				ctx.bind_pipeline(pipelines.at("ibl_specular_prefilter").pipeline());
				for(uint32_t mip = 0; mip < specular.mip_count(); ++mip) {
					const uint32_t resolution = std::max(
						specular.resolution() >> mip,
						1u
					);

					IBLPrefilterConstants constants {
						.source_handle = current_probe->capture().handle(),
						.output_handle = specular.storage_handle(mip),
						.resolution = resolution,
						.sample_count = mip == 0 ? 1u : 64u,
						.roughness = specular.mip_count() > 1 ? float(mip) / float(specular.mip_count() - 1) : 0.0f
					};

					ctx.push_constants(
						VK_SHADER_STAGE_ALL,
						&constants,
						sizeof(constants)
					);

					ctx.dispatch(
						(resolution + 7) / 8,
						(resolution + 7) / 8,
						LIGHT_PROBE_FACE_COUNT
					);
				}
			}
		)
		.add_compute_pass("ibl_diffuse_convolution" + pass_suffix,
			[&, current_probe](FrameGraph::ComputePassBuilder &pass) {
				pass.enabled_if([&, current_probe] {
					return light_probes.should_capture(*current_probe);
				});
				pass.read(current_probe->capture());
				pass.write(current_probe->diffuse());
			},
			[&, current_probe](FrameGraph::PassContext &ctx) {
				auto &diffuse = current_probe->diffuse();

				IBLIrradianceConstants constants {
					.source_handle = current_probe->capture().handle(),
					.output_handle = diffuse.storage_handle(0),
					.resolution = diffuse.resolution(),
					.sample_count = 128
				};


				ctx.bind_pipeline(pipelines.at("ibl_diffuse_convolution").pipeline());
				ctx.push_constants(
					VK_SHADER_STAGE_ALL,
					&constants,
					sizeof(constants)
				);
				ctx.dispatch(
					(constants.resolution + 7) / 8,
					(constants.resolution + 7) / 8,
					LIGHT_PROBE_FACE_COUNT
				);
				light_probes.finish_capture(*current_probe);
			}
		);
	}

	framegraph.add_compute_pass("ibl_brdf_lut",
			[&](FrameGraph::ComputePassBuilder &pass) {
				pass.enabled_if([&] { return light_probes.needs_brdf_lut(); });
				pass.write(light_probes.brdf_lut());
			},
			[&](FrameGraph::PassContext &ctx) {
				const auto extent = light_probes.brdf_lut().image().desc.extent;
				IBLBRDFConstants constants {
					.output_handle = light_probes.brdf_lut().handle(),
					.width = extent.width,
					.height = extent.height,
					.sample_count = 128
				};

				ctx.bind_pipeline(pipelines.at("ibl_brdf_lut").pipeline());
				ctx.push_constants(
					VK_SHADER_STAGE_ALL,
					&constants,
					sizeof(constants)
				);
				ctx.dispatch(
					(constants.width + 7) / 8,
					(constants.height + 7) / 8
				);
				light_probes.finish_brdf_lut();
			}
		)
		.add_render_pass("depth_prepass",
			[&](FrameGraph::RenderPassBuilder &pass) {
				pass.read(scene);
				pass.clear_depth(depth);
			},
			[&](FrameGraph::PassContext &ctx) {
				set_viewport_and_scissor(ctx, APP_WIDTH, APP_HEIGHT);

				PushConstants push_constants = {};
				push_constants.scene_handle = scene.handle();
				auto &backface_pipeline = pipelines.at("depth_prepass");
				auto &two_sided_pipeline = pipelines.at("depth_prepass_two_sided");
				scene_renderer.draw_depth_prepass(
					ctx,
					push_constants,
					backface_pipeline,
					two_sided_pipeline
				);
			}
		)
		.add_render_pass("gbuffer",
			[&](FrameGraph::RenderPassBuilder &pass) {
				pass.read(cascaded_shadowmap);
				pass.read(local_shadowmap);
				pass.read(scene);
				pass.read(lights);
				pass.read(local_shadow_matrices);
				pass.read(light_probes);
				for(const LightProbe &probe : light_probes.probes()) {
					pass.read(probe.specular());
					pass.read(probe.diffuse());
				}
				pass.read(light_probes.brdf_lut());
				pass.clear_color(color, {0.0f, 0.0f, 0.0f, 1.0f});
				pass.load_depth(depth);
			},
			[&](FrameGraph::PassContext &ctx){
				set_viewport_and_scissor(ctx, APP_WIDTH, APP_HEIGHT);

				PushConstants push_constants 	= {};
				push_constants.scene_handle 	= scene.handle();
				push_constants.light_handle 	= lights.handle();
				scene_renderer.draw_opaque(ctx, push_constants);
			}
		)
		.add_render_pass("skybox",
			[&](FrameGraph::RenderPassBuilder &pass) {
				pass.read(scene);
				pass.load_color(color);
				pass.load_depth(depth);
			},
			[&](FrameGraph::PassContext &ctx) {
				set_viewport_and_scissor(ctx, APP_WIDTH, APP_HEIGHT);

				PushConstants push_constants = {};
				push_constants.scene_handle = scene.handle();

				skybox_clouds.bind(ctx, push_constants);
				ctx.push_constants(
					VK_SHADER_STAGE_ALL,
					&push_constants,
					sizeof(PushConstants)
				);
				ctx.draw(3);
			}
		)
		.add_render_pass("swapchain_write",
			[&](FrameGraph::RenderPassBuilder &pass) {
				pass.read(color);
				pass.read(scene);
				pass.clear_color(swapchain_target, {0.0f, 0.0f, 0.0f, 1.0f});
			},
			[&](FrameGraph::PassContext &ctx){
				PushConstants push_constants = {};
				push_constants.vbo_handle 						= UINT32_MAX;
				push_constants.ibo_handle						= UINT32_MAX;
				push_constants.scene_handle 					= scene.handle();
				push_constants.object_handle 					= UINT32_MAX;

				swapchain_write_material.bind(ctx, push_constants);
				ctx.push_constants(VK_SHADER_STAGE_ALL, &push_constants, sizeof(PushConstants));
				ctx.draw(3);
			}
		)
		.add_render_pass("imgui_write",
			[&](FrameGraph::RenderPassBuilder &pass) {
				pass.load_color(swapchain_target);
			},
			[&](FrameGraph::PassContext &ctx){
				ImDrawData* draw_data = ImGui::GetDrawData();
				ImGui_ImplVulkan_RenderDrawData(draw_data, ctx);
			}
		)
		.compile();

	while(!quit) {
		while(SDL_PollEvent(&event)) {
			if(event.type == SDL_EVENT_QUIT)
				quit = true;
			camera.process_event(&event, window);
			ImGui_ImplSDL3_ProcessEvent(&event);

			if(event.type == SDL_EVENT_KEY_DOWN)
				if(event.key.key == SDLK_F4) {
					pipelines.rebuild_all();
					materials.reload_all();
					light_probes.mark_all_dirty();
					light_probes.invalidate_brdf_lut();
				}
		}
		camera.update();

		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();

		const bool lighting_changed = draw_lighting_widget(
			shadow_light_direction,
			shadow_light_color,
			shadow_light_intensity,
			editable_lights
		);

		if(lighting_changed)
			light_probes.mark_all_dirty();

		draw_light_probe_widget(light_probes);

		ImGui::ShowDemoWindow();
		ImGui::Render();

		VK_CHECK(device().waitForFences(1, &vkctx.frames[frame_index].fence, VK_TRUE, UINT64_MAX));
		VK_CHECK(device().resetFences(1, &vkctx.frames[frame_index].fence));
		VK_CHECK(device().resetCommandBuffer(vkctx.frames[frame_index].buffer, 0));
		light_probes.prepare();

		local_shadow_views.clear();
		for(uint32_t i = 0; i < LIGHT_COUNT; i++) {
			const auto &source = editable_lights[i];
			lights[i].position = glm::vec4(source.position, 1.0f);
			lights[i].direction = glm::vec4(glm::normalize(source.direction), 0.0f);
			lights[i].color = glm::vec4(source.color * source.intensity, 1.0f);
			lights[i].type = uint32_t(source.type);
			lights[i].range = source.range;
			lights[i].inner_cone_cos = glm::cos(glm::radians(source.inner_cone_degrees));
			lights[i].outer_cone_cos = glm::cos(glm::radians(source.outer_cone_degrees));
			lights[i].casts_shadow = source.casts_shadow ? 1u : 0u;
			lights[i].shadow_bias = source.shadow_bias;
			lights[i].shadow_padding = glm::vec2(0.0f);

			const float shadow_far = source.range > LOCAL_SHADOW_NEAR_PLANE
				? source.range
				: LOCAL_SHADOW_FALLBACK_FAR_PLANE;
			std::array<glm::mat4, LOCAL_SHADOW_FACE_COUNT> shadow_matrices;
			if(source.type == LightType::Point) {
				shadow_matrices = build_point_shadow_view_projections(
					source.position,
					LOCAL_SHADOW_NEAR_PLANE,
					shadow_far
				);
			} else {
				shadow_matrices.fill(glm::mat4(1.0f));
				shadow_matrices[0] = build_spot_shadow_view_projection(
					source.position,
					source.direction,
					glm::radians(source.outer_cone_degrees),
					LOCAL_SHADOW_NEAR_PLANE,
					shadow_far
				);
			}

			for(uint32_t face = 0; face < LOCAL_SHADOW_FACE_COUNT; face++) {
				const uint32_t layer = i * LOCAL_SHADOW_FACE_COUNT + face;
				local_shadow_matrices[layer].view_projection = shadow_matrices[face];
				if(source.casts_shadow &&
					(source.type == LightType::Point || face == 0))
				{
					local_shadow_views.push_back(LocalShadowView{
						.view_projection = shadow_matrices[face],
						.layer = layer,
					});
				}
			}
		}

		const float aspect_ratio = float(APP_WIDTH) / float(APP_HEIGHT);
		scene->projection = glm::perspectiveRH_ZO(CAMERA_FOV, aspect_ratio, CAMERA_NEAR, CAMERA_FAR);
		scene->projection[1][1] *= -1.0f;
		scene->view = camera.get_view_matrix();
		scene->camera_position = glm::vec4(camera.get_position(), 1.0f);

		const auto cascades = build_cascaded_shadow_data(
			camera.get_position(),
			camera.get_rotation_matrix(),
			CAMERA_FOV,
			aspect_ratio,
			CAMERA_NEAR,
			SHADOW_DISTANCE,
			shadow_light_direction,
			SHADOW_MAP_RESOLUTION
		);

		scene->shadow_view_projection = cascades.view_projection;
		scene->shadow_split_depths = cascades.split_depths;
		scene->shadow_light_direction = glm::vec4(shadow_light_direction, 0.0f);
		scene->shadow_light_color = glm::vec4(shadow_light_color * shadow_light_intensity, 0.0f);
		scene->shadowmap_handle = cascaded_shadowmap.handle();
		scene->shadow_cascade_count = SHADOW_CASCADE_COUNT;
		scene->shadowmap_resolution = SHADOW_MAP_RESOLUTION;
		scene->local_shadowmap_handle = local_shadowmap.handle();
		scene->local_shadowmap_resolution = LOCAL_SHADOW_MAP_RESOLUTION;
		scene->light_count = LIGHT_COUNT;
		scene->local_shadow_matrix_handle = local_shadow_matrices.handle();
		scene->local_shadow_padding = glm::vec2(0.0f);
		scene->reflection_probe_handle = light_probes.handle();
		scene->reflection_probe_count = uint32_t(light_probes.size());
		scene->reflection_probe_padding = 0;
		scene->ibl_brdf_lut_handle = light_probes.brdf_lut().handle();
		scene->ibl_padding = glm::vec2(0.0f);

		device().acquireNextImageKHR(
			vkctx.swapchain,
			UINT64_MAX,
			vkctx.frames[frame_index].acquire,
			VK_NULL_HANDLE,
			&image_index
		);

		static VkCommandBufferBeginInfo begin_info = {};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		device().beginCommandBuffer(vkctx.frames[frame_index].buffer, &begin_info);

		set_viewport_and_scissor(vkctx.frames[frame_index].buffer, APP_WIDTH, APP_HEIGHT);

		framegraph.execute(vkctx.frames[frame_index].buffer);

		// Transition swapchain target to be presentable (todo maybe make framegraph do this for me instead?)
		transition(vkctx.swapchain_images[image_index], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

		device().endCommandBuffer(vkctx.frames[frame_index].buffer);

		auto command_info = info::command_buffer_submit_info(vkctx.frames[frame_index].buffer);
		auto wait_info = info::semaphore_submit_info(
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
			vkctx.frames[frame_index].acquire
		);
		auto signal_info = info::semaphore_submit_info(
			VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
			vkctx.frames[frame_index].submit
		);

		auto submit_info = info::submit_info(&command_info, &signal_info, &wait_info);
		VK_CHECK(device().queueSubmit2(vkctx.device.get_queue(vkb::QueueType::graphics).value(), 1, &submit_info, vkctx.frames[frame_index].fence));

		VkPresentInfoKHR present_info = {};
		present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present_info.pSwapchains = &vkctx.swapchain.swapchain;
		present_info.swapchainCount = 1;
		present_info.pWaitSemaphores = &vkctx.frames[frame_index].submit;
		present_info.waitSemaphoreCount = 1;
		present_info.pImageIndices = &image_index;

		VK_CHECK(device().queuePresentKHR(vkctx.device.get_queue(vkb::QueueType::graphics).value(), &present_info));

		frame_index = (frame_index + 1) % vkctx.max_frames;
	}

	VK_CHECK(device().deviceWaitIdle());
	return 0;
}
