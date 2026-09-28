#include "gfx/texture/texture_cube.h"

#include <algorithm>
#include <stdexcept>

#include "gfx/command_list.h"
#include "gfx/device.h"
#include "gfx/texture/texture_internal.h"

namespace gfx {

TextureCube TextureCube::create(Device &device, const TextureCubeDesc &desc) {
	if(desc.resolution == 0 || desc.mip_count == 0)
		throw std::invalid_argument("TextureCube::create: invalid dimensions");

	uint32_t maximum_mips = 1;
	for(uint32_t size = desc.resolution; size > 1; size >>= 1)
		maximum_mips++;
	if(desc.mip_count > maximum_mips)
		throw std::invalid_argument("TextureCube::create: too many mip levels");

	const VkImageUsageFlags complete_usage =
		desc.usage |
		VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	auto image = texture_detail::create_image(
		device,
		ImageDesc{
			.image_type = VK_IMAGE_TYPE_2D,
			.view_type = VK_IMAGE_VIEW_TYPE_CUBE,
			.format = desc.format,
			.extent = {desc.resolution, desc.resolution, 1},
			.usage = complete_usage,
			.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
			.mip_count = desc.mip_count,
			.layer_count = 6,
		},
		VK_IMAGE_LAYOUT_GENERAL
	);
	const ImageRef sampled_view = image.ref();

	// Initialize every face and mip so a probe can safely use LOAD while its
	// expensive capture callback is skipped on clean frames.
	device.submit_and_wait([&](CommandList &commands) {
		VkClearColorValue clear = {};
		device.dispatch().cmdClearColorImage(
			commands.native(),
			sampled_view.image,
			VK_IMAGE_LAYOUT_GENERAL,
			&clear,
			1,
			&sampled_view.subresources
		);
	});

	ImageRef attachment;
	if(desc.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
		auto subresources = sampled_view.subresources;
		subresources.baseMipLevel = 0;
		subresources.levelCount = 1;
		attachment = image.create_view(
			VK_IMAGE_VIEW_TYPE_2D_ARRAY,
			subresources,
			6
		);
	}

	std::vector<ImageRef> storage_views;
	std::vector<uint32_t> storage_handles;
	if(desc.usage & VK_IMAGE_USAGE_STORAGE_BIT) {
		storage_views.reserve(desc.mip_count);
		storage_handles.reserve(desc.mip_count);
		for(uint32_t mip = 0; mip < desc.mip_count; mip++) {
			auto subresources = sampled_view.subresources;
			subresources.baseMipLevel = mip;
			subresources.levelCount = 1;
			ImageRef view = image.create_view(
				VK_IMAGE_VIEW_TYPE_2D_ARRAY,
				subresources,
				6
			);
			storage_handles.push_back(device.register_image(view));
			storage_views.push_back(view);
		}
	}

	texture_detail::set_image_debug_names(device, sampled_view, desc.name);
	if(attachment.valid()) {
		texture_detail::set_debug_name(
			device,
			VK_OBJECT_TYPE_IMAGE_VIEW,
			uint64_t(attachment.view),
			desc.name + "_attachment"
		);
	}
	for(uint32_t mip = 0; mip < storage_views.size(); mip++) {
		texture_detail::set_debug_name(
			device,
			VK_OBJECT_TYPE_IMAGE_VIEW,
			uint64_t(storage_views[mip].view),
			desc.name + "_storage_mip_" + std::to_string(mip)
		);
	}

	return TextureCube(M{
		.image = std::move(image),
		.attachment = attachment,
		.storage_views = std::move(storage_views),
		.storage_handles = std::move(storage_handles),
		.initial_layout = VK_IMAGE_LAYOUT_GENERAL,
		.handle = device.register_image(sampled_view),
		.resolution = desc.resolution,
		.mip_count = desc.mip_count,
		.name = desc.name,
	});
}

uint32_t TextureCube::storage_handle(uint32_t mip) const {
	if(mip >= m.storage_handles.size())
		throw std::out_of_range("TextureCube::storage_handle: mip is not storage-capable");
	return m.storage_handles[mip];
}

ImageRef TextureCube::attachment_view() const {
	if(!m.attachment.valid())
		throw std::runtime_error("TextureCube::attachment_view: cube is not an attachment");
	return m.attachment;
}

} // namespace gfx
