#include "gfx/texture/texture_internal.h"

#include <stdexcept>
#include <string>
#include <utility>

#include "gfx/command_list.h"
#include "gfx/device.h"
#include "gfx/raw_buffer.h"
#include "gfx/upload_batch.h"

namespace gfx {

VkImageLayout texture_detail::initial_layout_for(VkFormat format) {
	return (image_aspect_mask(format) & VK_IMAGE_ASPECT_DEPTH_BIT)
		? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
		: VK_IMAGE_LAYOUT_GENERAL;
}

Image texture_detail::create_image(
	UploadBatch &upload,
	ImageDesc desc,
	VkImageLayout initial_layout
) {
	auto image = Image::create(upload.device(), desc);
	const ImageRef ref = image.ref();
	upload.record([&](CommandList &commands) {
		commands.transition(
			ref.image,
			VK_IMAGE_LAYOUT_UNDEFINED,
			initial_layout,
			image_aspect_mask(desc.format)
		);
	});
	return image;
}

void texture_detail::upload_image(
	UploadBatch &upload,
	const ImageRef &image,
	VkExtent3D extent,
	VkFormat format,
	std::span<const std::byte> data,
	VkImageLayout final_layout
) {
	if(data.empty())
		throw std::invalid_argument("upload_image: data must not be empty");

	Device &device = upload.device();
	auto staging = RawBuffer::create(device, RawBufferDesc{
		.size = data.size_bytes(),
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		.host_visible = true,
	});
	staging.write(data);
	const VkBuffer staging_buffer = staging.native();
	upload.hold(std::move(staging));
	upload.record([&](CommandList &commands) {
		commands.transition(
			image.image,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL,
			image_aspect_mask(format)
		);
		VkBufferImageCopy copy = {};
		copy.imageSubresource.aspectMask = image_aspect_mask(format);
		copy.imageSubresource.mipLevel = 0;
		copy.imageSubresource.baseArrayLayer = 0;
		copy.imageSubresource.layerCount = 1;
		copy.imageExtent = extent;

		device.dispatch().cmdCopyBufferToImage(
			commands.native(),
			staging_buffer,
			image.image,
			VK_IMAGE_LAYOUT_GENERAL,
			1,
			&copy
		);
		commands.transition(
			image.image,
			VK_IMAGE_LAYOUT_GENERAL,
			final_layout,
			image_aspect_mask(format)
		);
	});
}

void texture_detail::set_debug_name(
	Device &device,
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
	device.dispatch().setDebugUtilsObjectNameEXT(&name_info);
}

void texture_detail::set_image_debug_names(
	Device &device,
	const ImageRef &image,
	std::string_view name
) {
	set_debug_name(device, VK_OBJECT_TYPE_IMAGE, uint64_t(image.image), name);
	set_debug_name(
		device,
		VK_OBJECT_TYPE_IMAGE_VIEW,
		uint64_t(image.view),
		std::string(name) + "_view"
	);
}

} // namespace gfx
