#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "gfx/image.h"

namespace gfx {

class Device;

struct TextureCubeDesc {
	uint32_t resolution = 0;
	uint32_t mip_count = 1;
	VkFormat format = VK_FORMAT_UNDEFINED;
	VkImageUsageFlags usage = 0;
	std::string name;
};

class TextureCube {
	struct M {
		Image image;
		ImageRef attachment;
		std::vector<ImageRef> storage_views;
		std::vector<uint32_t> storage_handles;
		VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		uint32_t handle = UINT32_MAX;
		uint32_t resolution = 0;
		uint32_t mip_count = 0;
		std::string name;
	} m;

	explicit TextureCube(M m) : m(std::move(m)) {}

public:
	static TextureCube create(Device &device, const TextureCubeDesc &desc);

	TextureCube(TextureCube &&) noexcept = default;
	TextureCube &operator=(TextureCube &&) noexcept = default;
	TextureCube(const TextureCube &) = delete;
	TextureCube &operator=(const TextureCube &) = delete;

	uint32_t handle() const { return m.handle; }
	uint32_t resolution() const { return m.resolution; }
	uint32_t mip_count() const { return m.mip_count; }
	uint32_t storage_handle(uint32_t mip) const;
	ImageRef image() const { return m.image.ref(); }
	ImageRef attachment_view() const;
	VkImageLayout initial_layout() const { return m.initial_layout; }
	const std::string &name() const { return m.name; }
};

} // namespace gfx
