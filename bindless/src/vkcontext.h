#pragma once

#include <spdlog/spdlog.h>
#include <vulkan/vulkan.h>
#include <VkBootstrap.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <functional>

#include "vkinfo.h"

struct AllocatedImage {
	VkImage image;
	VkImageView view;
	VmaAllocation allocation;
};

struct VulkanContext {
	struct FrameContext {
		VkFence fence;
		VkSemaphore submit;
		VkSemaphore acquire;
		VkCommandBuffer buffer;
	};

	vkb::Instance instance;
	vkb::Device device;
	vkb::Swapchain swapchain;
	VkSurfaceKHR surface;

	std::vector<VkImage> swapchain_images;
	std::vector<VkImageView> swapchain_views;

	VkCommandPool pool;
	uint32_t max_frames;
	std::vector<FrameContext> frames;

	VkCommandBuffer immediate_command_buffer;
	VkFence immediate_fence;

	VmaAllocator allocator;

	VkDescriptorPool global_bindless_pool;

	vkb::InstanceDispatchTable inst_disp;
	vkb::DispatchTable dev_disp;

	VkSampler sampler_address_repeat;
};

extern VulkanContext vkctx;
extern SDL_Window *window;
extern int frame_index;

vkb::DispatchTable &device();
vkb::InstanceDispatchTable &instance();

void submit_command(std::function<void(VkCommandBuffer buffer)> func);

void transition(VkCommandBuffer command, VkImage image, VkImageLayout current_layout, VkImageLayout new_layout);

struct PushConstants {
	uint32_t vbo_handle;
	uint32_t ibo_handle;
	uint32_t scene_handle;
	uint32_t object_handle;
	uint32_t light_handle;
	uint32_t material_handle;
	uint32_t material;
};

extern VkPipelineLayout global_layout;
extern uint32_t bindless_storage_index;
extern uint32_t bindless_texture_index;
extern std::vector<VkDescriptorSet> bindless_storage_desc;
extern std::vector<VkDescriptorSet> bindless_texture_desc;

static constexpr uint32_t max_bindings = 2 << 12;

VkDescriptorSetLayout create_bindless_desc_layout(std::vector<VkDescriptorSetLayoutBinding> bindings);
void create_bindless_desc(std::vector<VkDescriptorSetLayout> set_layouts, std::vector<VkDescriptorSet> *bindless_desc);
void update_descriptor_all_frames(VkWriteDescriptorSet write, const std::vector<VkDescriptorSet> &sets);

void create_vk_shit();
void create_imgui_shit();
