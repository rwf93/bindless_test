#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <utility>

#include "texture_common.h"

class TextureRegistry;

class Texture2D {
	struct M {
		GPUImage image;
		VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		uint32_t handle = UINT32_MAX;
		std::string name;
	} m;

	explicit Texture2D(M m) : m(std::move(m)) {}
	friend class TextureRegistry;

	static Texture2D create(
		uint32_t width,
		uint32_t height,
		VkFormat format,
		const std::function<uint32_t(int x, int y)> &function
	);
	static Texture2D create(
		uint32_t width,
		uint32_t height,
		VkFormat format,
		const std::function<uint32_t(int x, int y)> &function,
		const std::string &name
	);
	static Texture2D create(
		uint32_t width,
		uint32_t height,
		VkFormat format,
		std::span<const std::byte> data
	);
	static Texture2D create(
		uint32_t width,
		uint32_t height,
		VkFormat format,
		std::span<const std::byte> data,
		const std::string &name
	);

	static Texture2D create_empty(
		uint32_t width,
		uint32_t height,
		VkFormat format,
		VkImageUsageFlags usage
	);
	static Texture2D create_empty(
		uint32_t width,
		uint32_t height,
		VkFormat format,
		VkImageUsageFlags usage,
		const std::string &name
	);
	static Texture2D load_file(
		const std::filesystem::path &path,
		TextureColorSpace color_space
	);

public:
	Texture2D(Texture2D &&) noexcept = default;
	Texture2D &operator=(Texture2D &&) noexcept = default;
	Texture2D(const Texture2D &) = delete;
	Texture2D &operator=(const Texture2D &) = delete;

	uint32_t handle() const { return m.handle; }
	ImageRef image() const { return m.image.ref(); }
	VkImageLayout initial_layout() const { return m.initial_layout; }
	const std::string &name() const { return m.name; }
};
