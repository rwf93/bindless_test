#pragma once

#include <cstdint>
#include <filesystem>

#include "gfx/texture/texture_2d.h"

namespace gfx {

class UploadBatch;

enum class TextureColorSpace : uint8_t {
	Linear,
	SRGB,
};

class STBTextureLoader {
public:
	static Texture2D load(
		Device &device,
		const std::filesystem::path &path,
		TextureColorSpace color_space
	);
	static Texture2D load(
		UploadBatch &upload,
		const std::filesystem::path &path,
		TextureColorSpace color_space
	);
};

} // namespace gfx
