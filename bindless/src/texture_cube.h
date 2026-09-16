#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "texture_common.h"

class TextureRegistry;

class TextureCube {
	struct M {
		GPUImage image;
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
	friend class TextureRegistry;

	static TextureCube create_empty(
		uint32_t resolution,
		uint32_t mip_count,
		VkFormat format,
		VkImageUsageFlags usage,
		const std::string &name
	);

public:
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
