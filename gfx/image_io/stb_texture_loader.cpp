#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include "gfx/image_io/stb_texture_loader.h"

#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/gtc/packing.hpp>

namespace gfx {

Texture2D STBTextureLoader::load(
	Device &device,
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
				"STBTextureLoader::load: HDR texture '" + path_string +
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
				"STBTextureLoader::load: failed to load HDR texture '" +
				path_string + "'" +
				(reason ? ": " + std::string(reason) : std::string{})
			);
		}

		const size_t component_total = size_t(width) * height * 4;
		std::vector<uint16_t> half_pixels(component_total);
		for(size_t index = 0; index < component_total; index++)
			half_pixels[index] = glm::packHalf1x16(pixels.get()[index]);

		return Texture2D::create(
			device,
			Texture2DDesc{
				.width = uint32_t(width),
				.height = uint32_t(height),
				.format = VK_FORMAT_R16G16B16A16_SFLOAT,
				.name = path.filename().string(),
			},
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
			"STBTextureLoader::load: failed to load '" + path_string + "'" +
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
	return Texture2D::create(
		device,
		Texture2DDesc{
			.width = uint32_t(width),
			.height = uint32_t(height),
			.format = format,
			.name = path.filename().string(),
		},
		data
	);
}

} // namespace gfx
