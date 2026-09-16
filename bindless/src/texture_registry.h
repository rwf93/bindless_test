#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "texture_2d.h"
#include "texture_2d_array.h"
#include "texture_3d.h"
#include "texture_cube.h"

template<typename>
inline constexpr bool unsupported_texture_registry_type = false;

template<typename Texture>
inline constexpr bool supported_texture_registry_type =
	std::is_same_v<Texture, Texture2D> ||
	std::is_same_v<Texture, Texture2DArray> ||
	std::is_same_v<Texture, Texture3D> ||
	std::is_same_v<Texture, TextureCube>;

class TextureRegistry {
	struct M {
		std::unordered_map<std::string, Texture2D> cached_2d;
		std::unordered_map<std::string, Texture2D> named_2d;
		std::unordered_map<std::string, Texture2DArray> named_2d_arrays;
		std::unordered_map<std::string, Texture3D> named_3d;
		std::unordered_map<std::string, TextureCube> named_cubes;
	} m;

	explicit TextureRegistry(M m) : m(std::move(m)) {}

	Texture2D &create_impl(
		std::type_identity<Texture2D>,
		uint32_t width,
		uint32_t height,
		VkFormat format,
		std::function<uint32_t(int x, int y)> function,
		const std::string &name
	);
	Texture2D &create_impl(
		std::type_identity<Texture2D>,
		uint32_t width,
		uint32_t height,
		VkFormat format,
		std::span<const std::byte> data,
		const std::string &name
	);
	Texture2D &create_impl(
		std::type_identity<Texture2D>,
		uint32_t width,
		uint32_t height,
		VkFormat format,
		VkImageUsageFlags usage,
		const std::string &name
	);
	Texture2DArray &create_impl(
		std::type_identity<Texture2DArray>,
		uint32_t width,
		uint32_t height,
		uint32_t layer_count,
		VkFormat format,
		VkImageUsageFlags usage,
		const std::string &name
	);
	Texture3D &create_impl(
		std::type_identity<Texture3D>,
		uint32_t width,
		uint32_t height,
		uint32_t depth,
		VkFormat format,
		std::function<uint32_t(int x, int y, int z)> function,
		const std::string &name
	);
	Texture3D &create_impl(
		std::type_identity<Texture3D>,
		uint32_t width,
		uint32_t height,
		uint32_t depth,
		VkFormat format,
		std::span<const std::byte> data,
		const std::string &name
	);
	Texture3D &create_impl(
		std::type_identity<Texture3D>,
		uint32_t width,
		uint32_t height,
		uint32_t depth,
		VkFormat format,
		VkImageUsageFlags usage,
		const std::string &name
	);
	TextureCube &create_impl(
		std::type_identity<TextureCube>,
		uint32_t resolution,
		uint32_t mip_count,
		VkFormat format,
		VkImageUsageFlags usage,
		const std::string &name
	);

	Texture2D &load_2d(
		const std::filesystem::path &path,
		TextureColorSpace color_space
	);

public:
	TextureRegistry(TextureRegistry &&) noexcept = default;
	TextureRegistry &operator=(TextureRegistry &&) noexcept = default;
	TextureRegistry(const TextureRegistry &) = delete;
	TextureRegistry &operator=(const TextureRegistry &) = delete;

	// Creates a registry with the always-available missing, black, and white
	// textures already registered.
	static TextureRegistry create();

	template<typename Texture, typename... Args>
	Texture &create(Args &&...args) {
		if constexpr(supported_texture_registry_type<Texture>) {
			return create_impl(
				std::type_identity<Texture>{},
				std::forward<Args>(args)...
			);
		} else {
			static_assert(
				unsupported_texture_registry_type<Texture>,
				"TextureRegistry does not support this texture type"
			);
		}
	}

	template<typename Texture>
	Texture &load(
		const std::filesystem::path &path,
		TextureColorSpace color_space
	) {
		if constexpr(std::is_same_v<Texture, Texture2D>) {
			return load_2d(path, color_space);
		} else {
			static_assert(
				unsupported_texture_registry_type<Texture>,
				"TextureRegistry can only load Texture2D files"
			);
		}
	}

	template<typename Texture>
	Texture *find_named(std::string_view name) {
		const std::string key(name);
		if constexpr(std::is_same_v<Texture, Texture2D>) {
			auto found = m.named_2d.find(key);
			return found == m.named_2d.end() ? nullptr : &found->second;
		} else if constexpr(std::is_same_v<Texture, Texture2DArray>) {
			auto found = m.named_2d_arrays.find(key);
			return found == m.named_2d_arrays.end() ? nullptr : &found->second;
		} else if constexpr(std::is_same_v<Texture, Texture3D>) {
			auto found = m.named_3d.find(key);
			return found == m.named_3d.end() ? nullptr : &found->second;
		} else if constexpr(std::is_same_v<Texture, TextureCube>) {
			auto found = m.named_cubes.find(key);
			return found == m.named_cubes.end() ? nullptr : &found->second;
		} else {
			static_assert(
				unsupported_texture_registry_type<Texture>,
				"TextureRegistry does not support this texture type"
			);
		}
	}

	template<typename Texture>
	Texture &at_named(std::string_view name) {
		if(Texture *texture = find_named<Texture>(name))
			return *texture;
		throw std::out_of_range(
			"TextureRegistry::at_named: texture not found: " + std::string(name)
		);
	}
};
