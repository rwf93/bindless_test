#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include "texture_2d.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include <glm/gtc/packing.hpp>

#include "vkcontext.h"

namespace {

size_t uploaded_texel_size(VkFormat format) {
	switch(format) {
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

Texture2D Texture2D::create(
	uint32_t width,
	uint32_t height,
	VkFormat format,
	const std::function<uint32_t(int x, int y)> &function
) {
	std::vector<uint32_t> data(size_t(width) * height);
	for(uint32_t y = 0; y < height; y++) {
		for(uint32_t x = 0; x < width; x++)
			data[size_t(y) * width + x] = function(int(x), int(y));
	}
	return create(width, height, format, std::as_bytes(std::span(data)));
}

Texture2D Texture2D::create(
	uint32_t width,
	uint32_t height,
	VkFormat format,
	const std::function<uint32_t(int x, int y)> &function,
	const std::string &name
) {
	auto texture = create(width, height, format, function);
	texture.m.name = name;
	texture_detail::set_image_debug_names(texture.m.image.ref(), name);
	return texture;
}

Texture2D Texture2D::create(
	uint32_t width,
	uint32_t height,
	VkFormat format,
	std::span<const std::byte> data
) {
	const size_t expected_size =
		size_t(width) * height * uploaded_texel_size(format);
	if(data.size_bytes() != expected_size) {
		throw std::invalid_argument(
			"Texture2D::create: uploaded data size does not match its format and extent"
		);
	}

	auto image = texture_detail::create_image(
		ImageDesc{
			.image_type = VK_IMAGE_TYPE_2D,
			.view_type = VK_IMAGE_VIEW_TYPE_2D,
			.format = format,
			.extent = {width, height, 1},
			.usage =
				VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
				VK_IMAGE_USAGE_TRANSFER_DST_BIT |
				VK_IMAGE_USAGE_SAMPLED_BIT,
		},
		VK_IMAGE_LAYOUT_GENERAL
	);
	const ImageRef ref = image.ref();
	texture_detail::upload_image(
		ref,
		{width, height, 1},
		format,
		data,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);

	return Texture2D(M{
		.image = std::move(image),
		.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.handle = generate_handle(ref),
	});
}

Texture2D Texture2D::create(
	uint32_t width,
	uint32_t height,
	VkFormat format,
	std::span<const std::byte> data,
	const std::string &name
) {
	auto texture = create(width, height, format, data);
	texture.m.name = name;
	texture_detail::set_image_debug_names(texture.m.image.ref(), name);
	return texture;
}

Texture2D Texture2D::create_empty(
	uint32_t width,
	uint32_t height,
	VkFormat format,
	VkImageUsageFlags usage
) {
	const VkImageLayout initial_layout = texture_detail::initial_layout_for(format);
	auto image = texture_detail::create_image(
		ImageDesc{
			.image_type = VK_IMAGE_TYPE_2D,
			.view_type = VK_IMAGE_VIEW_TYPE_2D,
			.format = format,
			.extent = {width, height, 1},
			.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT,
		},
		initial_layout
	);
	const ImageRef ref = image.ref();

	return Texture2D(M{
		.image = std::move(image),
		.initial_layout = initial_layout,
		.handle = generate_handle(ref),
	});
}

Texture2D Texture2D::create_empty(
	uint32_t width,
	uint32_t height,
	VkFormat format,
	VkImageUsageFlags usage,
	const std::string &name
) {
	auto texture = create_empty(width, height, format, usage);
	texture.m.name = name;
	texture_detail::set_image_debug_names(texture.m.image.ref(), name);
	return texture;
}

Texture2D Texture2D::load_file(
	const std::filesystem::path &path,
	TextureColorSpace color_space
) {
	int width = 0;
	int height = 0;
	int component_count = 0;
	const std::string path_string = path.string();

	if(stbi_is_hdr(path_string.c_str())) {
		if(color_space != TextureColorSpace::Linear) {
			throw std::invalid_argument(
				"Texture2D::load_file: HDR texture '" + path_string +
				"' must use linear color space"
			);
		}

		using HDRPixels = std::unique_ptr<float, decltype(&stbi_image_free)>;
		HDRPixels pixels(
			stbi_loadf(
				path_string.c_str(),
				&width,
				&height,
				&component_count,
				4
			),
			&stbi_image_free
		);
		if(!pixels) {
			const char *reason = stbi_failure_reason();
			throw std::runtime_error(
				"Texture2D::load_file: failed to load HDR texture '" +
				path_string + "'" +
				(reason ? ": " + std::string(reason) : std::string{})
			);
		}

		// Radiance files decode to linear float32. Packing to float16 retains
		// their dynamic range while halving the sampled image's memory cost.
		const size_t component_total = size_t(width) * height * 4;
		std::vector<uint16_t> half_pixels(component_total);
		for(size_t index = 0; index < component_total; index++)
			half_pixels[index] = glm::packHalf1x16(pixels.get()[index]);

		return create(
			uint32_t(width),
			uint32_t(height),
			VK_FORMAT_R16G16B16A16_SFLOAT,
			std::as_bytes(std::span(half_pixels))
		);
	}

	using Pixels = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>;
	Pixels pixels(
		stbi_load(
			path_string.c_str(),
			&width,
			&height,
			&component_count,
			4
		),
		&stbi_image_free
	);
	if(!pixels) {
		const char *reason = stbi_failure_reason();
		throw std::runtime_error(
			"Texture2D::load_file: failed to load '" + path_string + "'" +
			(reason ? ": " + std::string(reason) : std::string{})
		);
	}

	const VkFormat format = color_space == TextureColorSpace::SRGB
		? VK_FORMAT_R8G8B8A8_SRGB
		: VK_FORMAT_R8G8B8A8_UNORM;
	const auto data = std::span(
		reinterpret_cast<const std::byte *>(pixels.get()),
		size_t(width) * height * 4
	);
	return create(uint32_t(width), uint32_t(height), format, data);
}
