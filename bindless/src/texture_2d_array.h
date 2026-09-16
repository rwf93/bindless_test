#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <utility>

#include "texture_common.h"

class TextureRegistry;

class Texture2DArray {
	struct M {
		GPUImage image;
		VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		uint32_t handle = UINT32_MAX;
		std::string name;
	} m;

	explicit Texture2DArray(M m) : m(std::move(m)) {}
	friend class TextureRegistry;

	static Texture2DArray create_empty(
		uint32_t width,
		uint32_t height,
		uint32_t layer_count,
		VkFormat format,
		VkImageUsageFlags usage
	);
	static Texture2DArray create_empty(
		uint32_t width,
		uint32_t height,
		uint32_t layer_count,
		VkFormat format,
		VkImageUsageFlags usage,
		const std::string &name
	);

public:
	Texture2DArray(Texture2DArray &&) noexcept = default;
	Texture2DArray &operator=(Texture2DArray &&) noexcept = default;
	Texture2DArray(const Texture2DArray &) = delete;
	Texture2DArray &operator=(const Texture2DArray &) = delete;

	uint32_t handle() const { return m.handle; }
	ImageRef image() const { return m.image.ref(); }
	VkImageLayout initial_layout() const { return m.initial_layout; }
	const std::string &name() const { return m.name; }
};
