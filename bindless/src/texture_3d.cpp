#include "texture_3d.h"

#include <stdexcept>
#include <vector>

#include "vkcontext.h"

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

Texture3D Texture3D::create(
	uint32_t width,
	uint32_t height,
	uint32_t depth,
	VkFormat format,
	const std::function<uint32_t(int x, int y, int z)> &function
) {
	std::vector<uint32_t> data(size_t(width) * height * depth);
	for(uint32_t z = 0; z < depth; z++) {
		for(uint32_t y = 0; y < height; y++) {
			for(uint32_t x = 0; x < width; x++) {
				const size_t index = (size_t(z) * height + y) * width + x;
				data[index] = function(int(x), int(y), int(z));
			}
		}
	}
	return create(width, height, depth, format, std::as_bytes(std::span(data)));
}

Texture3D Texture3D::create(
	uint32_t width,
	uint32_t height,
	uint32_t depth,
	VkFormat format,
	const std::function<uint32_t(int x, int y, int z)> &function,
	const std::string &name
) {
	auto texture = create(width, height, depth, format, function);
	texture.m.name = name;
	texture_detail::set_image_debug_names(texture.m.image.ref(), name);
	return texture;
}

Texture3D Texture3D::create(
	uint32_t width,
	uint32_t height,
	uint32_t depth,
	VkFormat format,
	std::span<const std::byte> data
) {
	const size_t expected_size = size_t(width) * height * depth * sizeof(uint32_t);
	if(data.size_bytes() != expected_size) {
		throw std::invalid_argument(
			"Texture3D::create: uploaded data must contain four bytes per texel"
		);
	}

	auto image = texture_detail::create_image(
		texture_3d_desc(
			width,
			height,
			depth,
			format,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
		),
		VK_IMAGE_LAYOUT_GENERAL
	);
	const ImageRef ref = image.ref();
	texture_detail::upload_image(
		ref,
		{width, height, depth},
		format,
		data,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);

	return Texture3D(M{
		.image = std::move(image),
		.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.handle = generate_handle(ref),
	});
}

Texture3D Texture3D::create(
	uint32_t width,
	uint32_t height,
	uint32_t depth,
	VkFormat format,
	std::span<const std::byte> data,
	const std::string &name
) {
	auto texture = create(width, height, depth, format, data);
	texture.m.name = name;
	texture_detail::set_image_debug_names(texture.m.image.ref(), name);
	return texture;
}

Texture3D Texture3D::create_empty(
	uint32_t width,
	uint32_t height,
	uint32_t depth,
	VkFormat format,
	VkImageUsageFlags usage
) {
	auto image = texture_detail::create_image(
		texture_3d_desc(
			width,
			height,
			depth,
			format,
			usage | VK_IMAGE_USAGE_SAMPLED_BIT
		),
		VK_IMAGE_LAYOUT_GENERAL
	);
	const ImageRef ref = image.ref();
	ImageRef attachment;
	if(usage & (
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
	)) {
		auto subresources = ref.subresources;
		subresources.layerCount = depth;
		attachment = image.create_view(
			VK_IMAGE_VIEW_TYPE_2D_ARRAY,
			subresources,
			depth
		);
	}

	return Texture3D(M{
		.image = std::move(image),
		.attachment = attachment,
		.initial_layout = VK_IMAGE_LAYOUT_GENERAL,
		.handle = generate_handle(ref),
	});
}

Texture3D Texture3D::create_empty(
	uint32_t width,
	uint32_t height,
	uint32_t depth,
	VkFormat format,
	VkImageUsageFlags usage,
	const std::string &name
) {
	auto texture = create_empty(width, height, depth, format, usage);
	texture.m.name = name;
	texture_detail::set_image_debug_names(texture.m.image.ref(), name);
	if(texture.m.attachment.valid()) {
		texture_detail::set_debug_name(
			VK_OBJECT_TYPE_IMAGE_VIEW,
			uint64_t(texture.m.attachment.view),
			name + "_attachment"
		);
	}
	return texture;
}

ImageRef Texture3D::attachment_view() const {
	if(!m.attachment.valid()) {
		throw std::runtime_error(
			"Texture3D::attachment_view: texture was not created with attachment usage"
		);
	}
	return m.attachment;
}
