#include "texture_common.h"

#include <cstring>
#include <stdexcept>
#include <string>

#include "vkcontext.h"
#include "vktools.h"

VkImageLayout texture_detail::initial_layout_for(VkFormat format) {
	return (image_aspect_mask(format) & VK_IMAGE_ASPECT_DEPTH_BIT)
		? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
		: VK_IMAGE_LAYOUT_GENERAL;
}

GPUImage texture_detail::create_image(
	ImageDesc desc,
	VkImageLayout initial_layout
) {
	auto image = GPUImage::create(desc);
	const ImageRef ref = image.ref();
	submit_command([&](VkCommandBuffer command) {
		transition(
			command,
			ref.image,
			VK_IMAGE_LAYOUT_UNDEFINED,
			initial_layout,
			image_aspect_mask(desc.format)
		);
	});
	return image;
}

void texture_detail::upload_image(
	const ImageRef &image,
	VkExtent3D extent,
	VkFormat format,
	std::span<const std::byte> data,
	VkImageLayout final_layout
) {
	if(data.empty())
		throw std::invalid_argument("upload_image: data must not be empty");

	const auto staging_allocation_info = info::allocation_create_info();
	const auto staging_buffer_info = info::buffer_create_info(
		VkDeviceSize(data.size_bytes()),
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT
	);

	VmaAllocation staging_allocation = VK_NULL_HANDLE;
	VkBuffer staging_buffer = VK_NULL_HANDLE;
	VK_CHECK(vmaCreateBuffer(
		vkctx.allocator,
		&staging_buffer_info,
		&staging_allocation_info,
		&staging_buffer,
		&staging_allocation,
		nullptr
	));

	void *mapped = nullptr;
	VK_CHECK(vmaMapMemory(vkctx.allocator, staging_allocation, &mapped));
	std::memcpy(mapped, data.data(), data.size_bytes());
	vmaUnmapMemory(vkctx.allocator, staging_allocation);

	submit_command([&](VkCommandBuffer command) {
		VkBufferImageCopy copy = {};
		copy.imageSubresource.aspectMask = image_aspect_mask(format);
		copy.imageSubresource.mipLevel = 0;
		copy.imageSubresource.baseArrayLayer = 0;
		copy.imageSubresource.layerCount = 1;
		copy.imageExtent = extent;

		device().cmdCopyBufferToImage(
			command,
			staging_buffer,
			image.image,
			VK_IMAGE_LAYOUT_GENERAL,
			1,
			&copy
		);
		transition(
			command,
			image.image,
			VK_IMAGE_LAYOUT_GENERAL,
			final_layout,
			image_aspect_mask(format)
		);
	});

	vmaDestroyBuffer(vkctx.allocator, staging_buffer, staging_allocation);
}

void texture_detail::set_debug_name(
	VkObjectType type,
	uint64_t handle,
	std::string_view name
) {
	if(handle == 0 || name.empty())
		return;

	const std::string owned_name(name);
	VkDebugUtilsObjectNameInfoEXT name_info = {};
	name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	name_info.objectType = type;
	name_info.objectHandle = handle;
	name_info.pObjectName = owned_name.c_str();
	device().setDebugUtilsObjectNameEXT(&name_info);
}

void texture_detail::set_image_debug_names(
	const ImageRef &image,
	std::string_view name
) {
	set_debug_name(VK_OBJECT_TYPE_IMAGE, uint64_t(image.image), name);
	set_debug_name(
		VK_OBJECT_TYPE_IMAGE_VIEW,
		uint64_t(image.view),
		std::string(name) + "_view"
	);
}
