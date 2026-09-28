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

struct Texture3DDesc {
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t depth = 0;
	Format format = Format::Undefined;
	TextureUsage usage = TextureUsage::None;
	std::string name;
};

class Texture3D {
	struct M {
		Image image;
		ImageRef attachment;
		VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		uint32_t handle = UINT32_MAX;
		std::string name;
	} m;

	explicit Texture3D(M m) : m(std::move(m)) {}

public:
	static Texture3D create(Device &device, const Texture3DDesc &desc);
	static Texture3D create(
		Device &device,
		const Texture3DDesc &desc,
		std::span<const std::byte> data
	);
	static Texture3D generate(
		Device &device,
		const Texture3DDesc &desc,
		const std::function<uint32_t(int x, int y, int z)> &function
	);

	Texture3D(Texture3D &&) noexcept = default;
	Texture3D &operator=(Texture3D &&) noexcept = default;
	Texture3D(const Texture3D &) = delete;
	Texture3D &operator=(const Texture3D &) = delete;

	uint32_t handle() const { return m.handle; }
	ImageRef image() const { return m.image.ref(); }
	VkImageLayout initial_layout() const { return m.initial_layout; }
	const std::string &name() const { return m.name; }
	ImageRef attachment_view() const;
};

} // namespace gfx
