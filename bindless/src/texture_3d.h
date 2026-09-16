#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <utility>

#include "texture_common.h"

class TextureRegistry;

class Texture3D {
	struct M {
		GPUImage image;
		ImageRef attachment;
		VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		uint32_t handle = UINT32_MAX;
		std::string name;
	} m;

	explicit Texture3D(M m) : m(std::move(m)) {}
	friend class TextureRegistry;

	static Texture3D create(
		uint32_t width,
		uint32_t height,
		uint32_t depth,
		VkFormat format,
		const std::function<uint32_t(int x, int y, int z)> &function
	);
	static Texture3D create(
		uint32_t width,
		uint32_t height,
		uint32_t depth,
		VkFormat format,
		const std::function<uint32_t(int x, int y, int z)> &function,
		const std::string &name
	);
	static Texture3D create(
		uint32_t width,
		uint32_t height,
		uint32_t depth,
		VkFormat format,
		std::span<const std::byte> data
	);
	static Texture3D create(
		uint32_t width,
		uint32_t height,
		uint32_t depth,
		VkFormat format,
		std::span<const std::byte> data,
		const std::string &name
	);

	static Texture3D create_empty(
		uint32_t width,
		uint32_t height,
		uint32_t depth,
		VkFormat format,
		VkImageUsageFlags usage
	);
	static Texture3D create_empty(
		uint32_t width,
		uint32_t height,
		uint32_t depth,
		VkFormat format,
		VkImageUsageFlags usage,
		const std::string &name
	);

public:
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
