#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <utility>

#include "gfx/image.h"
#include "gfx/types.h"

namespace gfx {

class Device;
class UploadBatch;

struct Texture2DDesc {
	uint32_t width = 0;
	uint32_t height = 0;
	Format format = Format::Undefined;
	TextureUsage usage = TextureUsage::None;
	std::string name;
};

class Texture2D {
	struct M {
		Image image;
		VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		uint32_t handle = UINT32_MAX;
		std::string name;
	} m;

	explicit Texture2D(M m) : m(std::move(m)) {}

public:
	static Texture2D create(Device &device, const Texture2DDesc &desc);
	static Texture2D create(UploadBatch &upload, const Texture2DDesc &desc);
	static Texture2D create(
		Device &device,
		const Texture2DDesc &desc,
		std::span<const std::byte> data
	);
	static Texture2D create(
		UploadBatch &upload,
		const Texture2DDesc &desc,
		std::span<const std::byte> data
	);
	static Texture2D generate(
		Device &device,
		const Texture2DDesc &desc,
		const std::function<uint32_t(int x, int y)> &function
	);
	static Texture2D generate(
		UploadBatch &upload,
		const Texture2DDesc &desc,
		const std::function<uint32_t(int x, int y)> &function
	);
	Texture2D(Texture2D &&) noexcept = default;
	Texture2D &operator=(Texture2D &&) noexcept = default;
	Texture2D(const Texture2D &) = delete;
	Texture2D &operator=(const Texture2D &) = delete;

	uint32_t handle() const { return m.handle; }
	ImageRef image() const { return m.image.ref(); }
	VkImageLayout initial_layout() const { return m.initial_layout; }
	const std::string &name() const { return m.name; }
};

} // namespace gfx
