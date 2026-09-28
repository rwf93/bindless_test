#include "gfx/texture/texture_2d.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "gfx/device.h"
#include "gfx/detail/types.h"
#include "gfx/texture/texture_internal.h"

namespace gfx {
namespace {

size_t uploaded_texel_size(Format format) {
	switch(detail::to_vk_format(format)) {
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8G8B8A8_SRGB:
		return 4;
	case VK_FORMAT_R16G16B16A16_SFLOAT:
		return 8;
	case VK_FORMAT_R32G32B32A32_SFLOAT:
		return 16;
	default:
		throw std::invalid_argument(
			"Texture2D::create: unsupported uploaded texture format"
		);
	}
}

} // namespace

Texture2D Texture2D::generate(
	Device &device,
	const Texture2DDesc &desc,
	const std::function<uint32_t(int x, int y)> &function
) {
	std::vector<uint32_t> data(size_t(desc.width) * desc.height);
	for(uint32_t y = 0; y < desc.height; y++) {
		for(uint32_t x = 0; x < desc.width; x++)
			data[size_t(y) * desc.width + x] = function(int(x), int(y));
	}
	return create(device, desc, std::as_bytes(std::span(data)));
}

Texture2D Texture2D::create(
	Device &device,
	const Texture2DDesc &desc,
	std::span<const std::byte> data
) {
	const size_t expected_size =
		size_t(desc.width) * desc.height * uploaded_texel_size(desc.format);
	if(data.size_bytes() != expected_size) {
		throw std::invalid_argument(
			"Texture2D::create: uploaded data size does not match its format and extent"
		);
	}

	auto image = texture_detail::create_image(
		device,
		ImageDesc{
			.image_type = VK_IMAGE_TYPE_2D,
			.view_type = VK_IMAGE_VIEW_TYPE_2D,
			.format = detail::to_vk_format(desc.format),
			.extent = {desc.width, desc.height, 1},
			.usage =
				detail::to_vk_image_usage(desc.usage) |
				VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
				VK_IMAGE_USAGE_TRANSFER_DST_BIT |
				VK_IMAGE_USAGE_SAMPLED_BIT,
		},
		VK_IMAGE_LAYOUT_GENERAL
	);
	const ImageRef ref = image.ref();
	texture_detail::upload_image(
		device,
		ref,
		{desc.width, desc.height, 1},
		detail::to_vk_format(desc.format),
		data,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);

	texture_detail::set_image_debug_names(device, ref, desc.name);
	return Texture2D(M{
		.image = std::move(image),
		.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.handle = device.register_image(ref),
		.name = desc.name,
	});
}

Texture2D Texture2D::create(Device &device, const Texture2DDesc &desc) {
	const VkImageLayout initial_layout = texture_detail::initial_layout_for(
		detail::to_vk_format(desc.format)
	);
	auto image = texture_detail::create_image(
		device,
		ImageDesc{
			.image_type = VK_IMAGE_TYPE_2D,
			.view_type = VK_IMAGE_VIEW_TYPE_2D,
			.format = detail::to_vk_format(desc.format),
			.extent = {desc.width, desc.height, 1},
			.usage = detail::to_vk_image_usage(desc.usage) | VK_IMAGE_USAGE_SAMPLED_BIT,
		},
		initial_layout
	);
	const ImageRef ref = image.ref();

	texture_detail::set_image_debug_names(device, ref, desc.name);
	return Texture2D(M{
		.image = std::move(image),
		.initial_layout = initial_layout,
		.handle = device.register_image(ref),
		.name = desc.name,
	});
}

} // namespace gfx
