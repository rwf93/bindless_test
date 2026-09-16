#include "texture_2d_array.h"

#include "vkcontext.h"

Texture2DArray Texture2DArray::create_empty(
	uint32_t width,
	uint32_t height,
	uint32_t layer_count,
	VkFormat format,
	VkImageUsageFlags usage
) {
	if(layer_count == 0) {
		throw std::invalid_argument(
			"Texture2DArray::create_empty: layer_count must be greater than zero"
		);
	}

	const VkImageLayout initial_layout = texture_detail::initial_layout_for(format);
	auto image = texture_detail::create_image(
		ImageDesc{
			.image_type = VK_IMAGE_TYPE_2D,
			.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
			.format = format,
			.extent = {width, height, 1},
			.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT,
			.layer_count = layer_count,
		},
		initial_layout
	);
	const ImageRef ref = image.ref();

	return Texture2DArray(M{
		.image = std::move(image),
		.initial_layout = initial_layout,
		.handle = generate_handle(ref),
	});
}

Texture2DArray Texture2DArray::create_empty(
	uint32_t width,
	uint32_t height,
	uint32_t layer_count,
	VkFormat format,
	VkImageUsageFlags usage,
	const std::string &name
) {
	auto texture = create_empty(width, height, layer_count, format, usage);
	texture.m.name = name;
	texture_detail::set_image_debug_names(texture.m.image.ref(), name);
	return texture;
}
