#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>

// This code gracefully goes to vkguide.dev

namespace info {

VkCommandPoolCreateInfo command_pool_create_info(uint32_t queue_family_index, VkCommandPoolCreateFlags flags = 0);
VkCommandBufferAllocateInfo command_buffer_allocate_info(
	VkCommandPool command_pool,
	uint32_t count = 1,
	VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY
);

VkBufferCreateInfo buffer_create_info(
	VkDeviceSize size,
	VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	VkBufferCreateFlags flags = 0
);
VmaAllocationCreateInfo allocation_create_info(
	VmaAllocationCreateFlags flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
	VkMemoryPropertyFlags preferred_flags = 0,
	VmaMemoryUsage usage = VMA_MEMORY_USAGE_AUTO,
	float priority = 1.0f
);

VkDescriptorSetLayoutBinding descriptor_set_layout_binding(
	VkDescriptorType type,
	VkShaderStageFlags stage_flags,
	uint32_t binding,
	uint32_t count = 1
);
VkDescriptorSetAllocateInfo descriptor_set_allocate_info(
	std::vector<VkDescriptorSetLayout> &allocate_info,
	VkDescriptorPool descriptor_pool
);
VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info(
	std::vector<VkDescriptorSetLayoutBinding> &layout_info,
	VkDescriptorSetLayoutCreateFlags flags = 0
);

VkViewport viewport(float width, float height, float x = 0, float y = 0, float min_depth = 0.0f, float max_depth = 1.0f);
VkRect2D rect2d(VkOffset2D offset, VkExtent2D extent); // mainly used for scissors, might be useless

VkPipelineLayoutCreateInfo pipeline_layout_info(std::vector<VkDescriptorSetLayout> &descriptor_layouts);

VkPipelineVertexInputStateCreateInfo input_vertex_info(
	std::vector<VkVertexInputBindingDescription> &bindings,
	std::vector<VkVertexInputAttributeDescription> &attributes
);

VkPipelineRenderingCreateInfoKHR rendering_create_info(
	std::vector<VkFormat> &color_attachment_formats,
	VkFormat depth_format,
	VkFormat stencil_format = VK_FORMAT_UNDEFINED
);

VkImageCreateInfo image_create_info(int width, int height, int depth = 1);
VkImageCreateInfo image_create_info(VkExtent2D extent);

VkImageSubresourceRange image_subresource_range(VkImageAspectFlags aspect_mask);

VkSemaphoreSubmitInfo semaphore_submit_info(VkPipelineStageFlags2 stage_mask, VkSemaphore semaphore);
VkCommandBufferSubmitInfo command_buffer_submit_info(VkCommandBuffer command);
VkSubmitInfo2 submit_info(
	VkCommandBufferSubmitInfo *command,
	VkSemaphoreSubmitInfo *signal_semaphore_info,
	VkSemaphoreSubmitInfo *wait_semaphore_info
);

VkRenderingAttachmentInfo attachment_info(VkImageView view, VkClearValue *clear, VkImageLayout layout);
VkRenderingInfo rendering_info(
	VkExtent2D extent,
	VkRenderingAttachmentInfo *color_attachments,
	VkRenderingAttachmentInfo *depth_attachments,
	uint32_t color_attachment_count = 1
);

}