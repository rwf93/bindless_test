#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <utility>

#include "gfx/image.h"
#include "gfx/types.h"

namespace gfx {

class Device;
class UploadBatch;

struct Texture2DArrayDesc {
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t layer_count = 0;
	Format format = Format::Undefined;
	TextureUsage usage = TextureUsage::None;
	std::string name;
};

class Texture2DArray {
	struct M {
		Image image;
		VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		uint32_t handle = UINT32_MAX;
		std::string name;
	} m;

	explicit Texture2DArray(M m) : m(std::move(m)) {}

public:
	static Texture2DArray create(Device &device, const Texture2DArrayDesc &desc);
	static Texture2DArray create(UploadBatch &upload, const Texture2DArrayDesc &desc);

	Texture2DArray(Texture2DArray &&) noexcept = default;
	Texture2DArray &operator=(Texture2DArray &&) noexcept = default;
	Texture2DArray(const Texture2DArray &) = delete;
	Texture2DArray &operator=(const Texture2DArray &) = delete;

	uint32_t handle() const { return m.handle; }
	ImageRef image() const { return m.image.ref(); }
	VkImageLayout initial_layout() const { return m.initial_layout; }
	const std::string &name() const { return m.name; }
};

} // namespace gfx
