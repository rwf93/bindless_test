#include "gfx/texture/texture_3d.h"

#include <stdexcept>
#include <vector>

#include "gfx/device.h"
#include "gfx/texture/texture_internal.h"

namespace gfx {
namespace {

ImageDesc texture_3d_desc(
	uint32_t width,
	uint32_t height,
	uint32_t depth,
	VkFormat format,
	VkImageUsageFlags usage
) {
	return ImageDesc{
		.image_type = VK_IMAGE_TYPE_3D,
		.view_type = VK_IMAGE_VIEW_TYPE_3D,
		.format = format,
		.extent = {width, height, depth},
		.usage = usage,
		.flags = VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT,
	};
}

} // namespace

Texture3D Texture3D::generate(
	Device &device,
	const Texture3DDesc &desc,
	const std::function<uint32_t(int x, int y, int z)> &function
) {
	std::vector<uint32_t> data(size_t(desc.width) * desc.height * desc.depth);
	for(uint32_t z = 0; z < desc.depth; z++) {
		for(uint32_t y = 0; y < desc.height; y++) {
			for(uint32_t x = 0; x < desc.width; x++) {
				const size_t index = (size_t(z) * desc.height + y) * desc.width + x;
				data[index] = function(int(x), int(y), int(z));
			}
		}
	}
	return create(device, desc, std::as_bytes(std::span(data)));
}

Texture3D Texture3D::create(
	Device &device,
	const Texture3DDesc &desc,
	std::span<const std::byte> data
) {
	const size_t expected_size =
		size_t(desc.width) * desc.height * desc.depth * sizeof(uint32_t);
	if(data.size_bytes() != expected_size) {
		throw std::invalid_argument(
			"Texture3D::create: uploaded data must contain four bytes per texel"
		);
	}

	auto image = texture_detail::create_image(
		device,
		texture_3d_desc(
			desc.width,
			desc.height,
			desc.depth,
			desc.format,
			desc.usage |
				VK_IMAGE_USAGE_TRANSFER_DST_BIT |
				VK_IMAGE_USAGE_SAMPLED_BIT
		),
		VK_IMAGE_LAYOUT_GENERAL
	);
	const ImageRef ref = image.ref();
	texture_detail::upload_image(
		device,
		ref,
		{desc.width, desc.height, desc.depth},
		desc.format,
		data,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);

	texture_detail::set_image_debug_names(device, ref, desc.name);
	return Texture3D(M{
		.image = std::move(image),
		.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.handle = device.register_image(ref),
		.name = desc.name,
	});
}

Texture3D Texture3D::create(Device &device, const Texture3DDesc &desc) {
	auto image = texture_detail::create_image(
		device,
		texture_3d_desc(
			desc.width,
			desc.height,
			desc.depth,
			desc.format,
			desc.usage | VK_IMAGE_USAGE_SAMPLED_BIT
		),
		VK_IMAGE_LAYOUT_GENERAL
	);
	const ImageRef ref = image.ref();
	ImageRef attachment;
	if(desc.usage & (
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
	)) {
		auto subresources = ref.subresources;
		subresources.layerCount = desc.depth;
		attachment = image.create_view(
			VK_IMAGE_VIEW_TYPE_2D_ARRAY,
			subresources,
			desc.depth
		);
	}

	texture_detail::set_image_debug_names(device, ref, desc.name);
	if(attachment.valid()) {
		texture_detail::set_debug_name(
			device,
			VK_OBJECT_TYPE_IMAGE_VIEW,
			uint64_t(attachment.view),
			desc.name + "_attachment"
		);
	}
	return Texture3D(M{
		.image = std::move(image),
		.attachment = attachment,
		.initial_layout = VK_IMAGE_LAYOUT_GENERAL,
		.handle = device.register_image(ref),
		.name = desc.name,
	});
}

ImageRef Texture3D::attachment_view() const {
	if(!m.attachment.valid()) {
		throw std::runtime_error(
			"Texture3D::attachment_view: texture was not created with attachment usage"
		);
	}
	return m.attachment;
}

} // namespace gfx
