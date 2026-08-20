#include <spdlog/spdlog.h>
#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include "camera.h"
#include "vktools.h"
#include "vkinfo.h"
#include "vkcontext.h"
#include "shader.h"
#include "resource.h"
#include "framegraph.h"
#include "material.h"
#include "model.h"
#include "vfs.h"

static SDL_Event event;
static uint32_t image_index;
static bool quit = false;

// Convenience overload that uses the current frame's command buffer.
void transition(VkImage image, VkImageLayout current_layout, VkImageLayout new_layout) {
	transition(vkctx.frames[frame_index].buffer, image, current_layout, new_layout);
}

int main(int argc, char** argv) {
	if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
		spdlog::error("Couldn't initalize SDL");
	}

	window = SDL_CreateWindow("fuck", 1600, 900, SDL_WINDOW_VULKAN);
	SDL_ShowWindow(window);

	create_vk_shit();
	auto vfs = VFS::create("C:\\Users\\rwf93\\Desktop\\bindless_test\\bindless\\vfs.toml");
	Pipeline::rebuild_all(vfs);
	create_imgui_shit();

	auto color = Texture2D::create_empty(
		1600, 900,
		VK_FORMAT_R16G16B16A16_UNORM,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		"gbuffer_albedo"
	);

	auto normal = Texture2D::create_empty(
		1600, 900,
		VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		"gbuffer_normal"
	);

	auto depth = Texture2D::create_empty(
		1600,
		900,
		VK_FORMAT_D32_SFLOAT,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		"depth"
	);

	auto shadowmap = Texture2D::create_empty(
		1024,
		1024,
		VK_FORMAT_D32_SFLOAT,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
	);

	auto camera = Camera::create();

	/* Scene specific info */
	auto scene = SharedBuffer<SceneData>::create(1);
	auto lights = SharedBuffer<LightData>::create(12);

	auto helmet = Model::create(vfs, "models/DamagedHelmet/glTF/DamagedHelmet.gltf");
	auto &helmet_pbr = Material::load_toml(vfs, "materials/helmet_pbr.toml");
	auto &helmet_unlit = Material::load_toml(vfs, "materials/helmet_unlit.toml");

	auto avocado = Model::create(vfs, "models/Avocado/glTF/Avocado.gltf");
	auto &avocado_unlit = Material::load_toml(vfs, "materials/avocado_unlit.toml");
	auto &avocado_pbr = Material::load_toml(vfs, "materials/avocado_pbr.toml");

	auto &swapchain_write_material = Material::load_toml(vfs, "materials/swapchain_write.toml");

	lights[0].position = glm::vec4(-10.0f,  10.0f, 10.0f, 1.0f),
	lights[1].position = glm::vec4( 10.0f,  10.0f, 10.0f, 1.0f),
	lights[2].position = glm::vec4(-10.0f, -10.0f, 10.0f, 1.0f),
	lights[3].position = glm::vec4( 10.0f, -10.0f, 10.0f, 1.0f),

	lights[0].color = glm::vec4(300, 300, 300, 255);
	lights[1].color = glm::vec4(300, 300, 300, 255);
	lights[2].color = glm::vec4(300, 300, 300, 255);
	lights[3].color = glm::vec4(300, 300, 300, 255);

	auto framegraph = FrameGraph::create()
		.add_resource("color",  	color.image(),  VK_FORMAT_R16G16B16A16_UNORM, VkExtent2D{1600, 900})
		.add_resource("normal", 	normal.image(), VK_FORMAT_R8G8B8A8_UNORM,     VkExtent2D{1600, 900})
		.add_resource("depth",  	depth.image(),  VK_FORMAT_D32_SFLOAT,         VkExtent2D{1600, 900})
		.add_resource("swapchain", 	VK_FORMAT_B8G8R8A8_UNORM, 		VkExtent2D{1600, 900})
		.add_render_pass("skybox",
			{},
			{
				FrameGraph::Output{
					.name = "color",
					.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
					.store_op = VK_ATTACHMENT_STORE_OP_STORE,
					.clear_value = {0.0f, 0.0f, 0.0f, 1.0f}
				},
			},
			[&](FrameGraph::RenderContext &ctx) {
				PushConstants push_constants 	= {};
				push_constants.scene_handle 	= scene.handle();
				push_constants.light_handle 	= lights.handle();

				ctx.bind_pipeline(Pipeline::registry["skybox"].pipeline());
				ctx.push_constants(VK_SHADER_STAGE_ALL, &push_constants, sizeof(PushConstants));
				ctx.draw(3);
			}
		)
		.add_render_pass("gbuffer",
			{},
			{
				FrameGraph::Output{
					.name = "color",
					.load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
					.store_op = VK_ATTACHMENT_STORE_OP_STORE,
					.clear_value = {0.0f, 0.0f, 0.0f, 1.0f}
				},
				FrameGraph::Output{
					.name = "normal",
					.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
					.store_op = VK_ATTACHMENT_STORE_OP_STORE,
					.clear_value = {0.0f, 0.0f, 0.0f, 1.0f}
				},
				FrameGraph::Output{
					.name = "depth",
					.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
					.store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE,
					.clear_value = {1.0f, 1.0f, 1.0f, 1.0f}
				}
			},
			[&](FrameGraph::RenderContext &ctx){
				scene->projection = glm::perspective(
					glm::radians(70.0f),
					1600.0f / 900.0f,
					0.1f,
					5000.0f
				);
				scene->projection[1][1] *= -1;

				scene->view = camera.get_view_matrix();
				scene->camera_position = glm::vec4(camera.get_position(), 0.0f);

				PushConstants push_constants 	= {};
				push_constants.scene_handle 	= scene.handle();
				push_constants.light_handle 	= lights.handle();

				helmet.reset();
				helmet.draw(ctx, push_constants, helmet_pbr);
				helmet.draw(ctx, push_constants, helmet_pbr,   glm::translate(glm::mat4(1.0f), glm::vec3( 5.0f, 0.0f, 0.0f)));
				helmet.draw(ctx, push_constants, helmet_unlit,   glm::translate(glm::mat4(1.0f), glm::vec3(-5.0f, 0.0f, 0.0f)));

				avocado.reset();
				avocado.draw(ctx, push_constants, avocado_unlit, glm::translate(glm::mat4(1.0f), glm::vec3(0.0, 5.0, 0.0f)));
			}
		)
		.add_render_pass("swapchain_write",
			{
				FrameGraph::Input{
					.name = "color"
				},
				FrameGraph::Input{
					.name = "normal"
				},
				FrameGraph::Input{
					.name = "depth"
				}
			},
			{
				FrameGraph::Output{
					.name = "swapchain",
					.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
					.store_op = VK_ATTACHMENT_STORE_OP_STORE,
					.clear_value = {0.0, 0.0, 0.0, 1.0}
				}
			},
			[&](FrameGraph::RenderContext &ctx){
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
			{},
			{
				FrameGraph::Output{
					.name = "swapchain",
					.load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
					.store_op = VK_ATTACHMENT_STORE_OP_STORE,
					.clear_value = {0.0, 0.0, 0.0, 0.0}
				}
			},
			[&](FrameGraph::RenderContext &ctx){
				ImGui_ImplVulkan_NewFrame();
				ImGui_ImplSDL3_NewFrame();
				ImGui::NewFrame();

				ImGui::Render();

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
					Pipeline::rebuild_all(vfs);
					Material::reload_all(vfs);
				}
		}
		camera.update();

		VK_CHECK(device().waitForFences(1, &vkctx.frames[frame_index].fence, VK_TRUE, UINT64_MAX));
		VK_CHECK(device().resetFences(1, &vkctx.frames[frame_index].fence));
		VK_CHECK(device().resetCommandBuffer(vkctx.frames[frame_index].buffer, 0));

		static VkCommandBufferBeginInfo begin_info = {};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		device().acquireNextImageKHR(
			vkctx.swapchain,
			UINT64_MAX,
			vkctx.frames[frame_index].acquire,
			VK_NULL_HANDLE,
			&image_index
		);

		device().beginCommandBuffer(vkctx.frames[frame_index].buffer, &begin_info);

		VkRect2D scissor = { {0, 0}, {1600, 900} };
		VkViewport viewport = {0, 0, 1600, 900, 0, 1};
		device().cmdSetScissor(
			vkctx.frames[frame_index].buffer,
			0,
			1,
			&scissor
		);

		device().cmdSetViewport(
			vkctx.frames[frame_index].buffer,
			0,
			1,
			&viewport
		);

		VkDescriptorSet bound_sets[] = {
			bindless_storage_desc[frame_index],
			bindless_texture_desc[frame_index]
		};

		device().cmdBindDescriptorSets(
			vkctx.frames[frame_index].buffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			global_layout,
			0,
			2,
			bound_sets,
			0,
			nullptr
		);

		framegraph
			.update_resource("swapchain", {vkctx.swapchain_images[image_index], vkctx.swapchain_views[image_index]})
			.execute(vkctx.frames[frame_index].buffer);

		transition(vkctx.swapchain_images[image_index], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
		device().endCommandBuffer(vkctx.frames[frame_index].buffer);

		auto command_info = info::command_buffer_submit_info(vkctx.frames[frame_index].buffer);
		auto wait_info = info::semaphore_submit_info(
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
			vkctx.frames[frame_index].acquire
		);
		auto signal_info = info::semaphore_submit_info(
			VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
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

	return 0;
}
