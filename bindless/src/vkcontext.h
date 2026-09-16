#pragma once

#include <spdlog/spdlog.h>
#include <vulkan/vulkan.h>
#include <VkBootstrap.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <functional>

#include "gpu_image.h"
#include "shader_types.h"
#include "vkinfo.h"

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
};

extern VulkanContext vkctx;
extern SDL_Window *window;
extern int frame_index;

vkb::DispatchTable &device();
vkb::InstanceDispatchTable &instance();

void submit_command(std::function<void(VkCommandBuffer buffer)> func);

VkImageAspectFlags image_aspect_mask(VkFormat format);
void transition(
	VkCommandBuffer command,
	VkImage image,
	VkImageLayout current_layout,
	VkImageLayout new_layout,
	VkImageAspectFlags aspect_mask
);

extern VkPipelineLayout global_layout;
extern uint32_t bindless_storage_index;
extern uint32_t bindless_texture_index;
extern std::vector<VkDescriptorSet> bindless_desc;

static constexpr uint32_t max_bindings = 2 << 12;

VkDescriptorSetLayout create_bindless_desc_layout(std::vector<VkDescriptorSetLayoutBinding> bindings);
void create_bindless_desc(std::vector<VkDescriptorSetLayout> set_layouts, std::vector<VkDescriptorSet> *bindless_desc);
void update_descriptor_all_frames(VkWriteDescriptorSet write, const std::vector<VkDescriptorSet> &sets);

template<typename T>
static uint32_t generate_handle(uint32_t count, VkBuffer buffer) {
	VkDescriptorBufferInfo buffer_info_desc = {};
	buffer_info_desc.buffer = buffer;
	buffer_info_desc.offset = 0;
	buffer_info_desc.range = sizeof(T) * count;

	VkWriteDescriptorSet write_desc = {};
	write_desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write_desc.dstBinding = 7;
	write_desc.dstArrayElement = bindless_storage_index;
	write_desc.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	write_desc.descriptorCount = 1;
	write_desc.pBufferInfo = &buffer_info_desc;

	update_descriptor_all_frames(write_desc, bindless_desc);

	return bindless_storage_index++;
}

static uint32_t generate_handle(const ImageRef &image) {
	const uint32_t descriptor_index = bindless_texture_index++;

	VkDescriptorImageInfo image_info = {};
	image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	image_info.imageView = image.view;

	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstBinding = 2;
	write.dstArrayElement = descriptor_index;
	write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	write.descriptorCount = 1;
	write.pImageInfo = &image_info;

	update_descriptor_all_frames(write, bindless_desc);

	// A storage-capable texture occupies the same numerical slot in binding 3.
	// Slang selects binding 2 or 3 from the DescriptorHandle's resource type, so
	// one handle can be used as Texture* when sampled and RWTexture* when written.
	if(image.desc.usage & VK_IMAGE_USAGE_STORAGE_BIT) {
		VkDescriptorImageInfo storage_info = {};
		storage_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		storage_info.imageView = image.view;

		VkWriteDescriptorSet storage_write = {};
		storage_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		storage_write.dstBinding = 3;
		storage_write.dstArrayElement = descriptor_index;
		storage_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		storage_write.descriptorCount = 1;
		storage_write.pImageInfo = &storage_info;

		update_descriptor_all_frames(storage_write, bindless_desc);
	}

	return descriptor_index;
}

void create_vk_shit();
void create_imgui_shit();
