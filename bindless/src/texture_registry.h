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

#include "gfx/image_io/stb_texture_loader.h"
#include "gfx/texture/texture.h"
#include "gfx/upload_batch.h"

namespace texture_registry_detail {
template<typename>
inline constexpr bool unsupported_type = false;

template<typename Texture>
inline constexpr bool supported_type =
	std::is_same_v<Texture, gfx::Texture2D> ||
	std::is_same_v<Texture, gfx::Texture2DArray> ||
	std::is_same_v<Texture, gfx::Texture3D> ||
	std::is_same_v<Texture, gfx::TextureCube>;
}

class TextureRegistry {
	struct M {
		gfx::Device *device = nullptr;
		std::unordered_map<std::string, gfx::Texture2D> cached_2d;
		std::unordered_map<std::string, gfx::Texture2D> named_2d;
		std::unordered_map<std::string, gfx::Texture2DArray> named_2d_arrays;
		std::unordered_map<std::string, gfx::Texture3D> named_3d;
		std::unordered_map<std::string, gfx::TextureCube> named_cubes;
	} m;

	explicit TextureRegistry(M m) : m(std::move(m)) {}

	gfx::Texture2D &create_impl(
		gfx::UploadBatch &upload,
		std::type_identity<gfx::Texture2D>,
		const gfx::Texture2DDesc &desc,
		std::function<uint32_t(int x, int y)> function
	);
	gfx::Texture2D &create_impl(
		gfx::UploadBatch &upload,
		std::type_identity<gfx::Texture2D>,
		const gfx::Texture2DDesc &desc,
		std::span<const std::byte> data
	);
	gfx::Texture2D &create_impl(
		gfx::UploadBatch &upload,
		std::type_identity<gfx::Texture2D>,
		const gfx::Texture2DDesc &desc
	);
	gfx::Texture2DArray &create_impl(
		gfx::UploadBatch &upload,
		std::type_identity<gfx::Texture2DArray>,
		const gfx::Texture2DArrayDesc &desc
	);
	gfx::Texture3D &create_impl(
		gfx::UploadBatch &upload,
		std::type_identity<gfx::Texture3D>,
		const gfx::Texture3DDesc &desc,
		std::function<uint32_t(int x, int y, int z)> function
	);
	gfx::Texture3D &create_impl(
		gfx::UploadBatch &upload,
		std::type_identity<gfx::Texture3D>,
		const gfx::Texture3DDesc &desc,
		std::span<const std::byte> data
	);
	gfx::Texture3D &create_impl(
		gfx::UploadBatch &upload,
		std::type_identity<gfx::Texture3D>,
		const gfx::Texture3DDesc &desc
	);
	gfx::TextureCube &create_impl(
		gfx::UploadBatch &upload,
		std::type_identity<gfx::TextureCube>,
		const gfx::TextureCubeDesc &desc
	);

	gfx::Texture2D &load_2d(
		gfx::UploadBatch &upload,
		const std::filesystem::path &path,
		gfx::TextureColorSpace color_space
	);

public:
	TextureRegistry(TextureRegistry &&) noexcept = default;
	TextureRegistry &operator=(TextureRegistry &&) noexcept = default;
	TextureRegistry(const TextureRegistry &) = delete;
	TextureRegistry &operator=(const TextureRegistry &) = delete;

	// Creates a registry with the always-available missing, black, and white
	// textures already registered.
	static TextureRegistry create(gfx::Device &device);
	static TextureRegistry create(gfx::UploadBatch &upload);
	gfx::Device &device() const { return *m.device; }

	template<typename Texture, typename... Args>
	Texture &create(Args &&...args) {
		auto upload = gfx::UploadBatch::create(*m.device);
		Texture &result = create<Texture>(upload, std::forward<Args>(args)...);
		upload.submit().wait();
		return result;
	}

	template<typename Texture, typename... Args>
	Texture &create(gfx::UploadBatch &upload, Args &&...args) {
		if constexpr(texture_registry_detail::supported_type<Texture>) {
			return create_impl(
				upload,
				std::type_identity<Texture>{},
				std::forward<Args>(args)...
			);
		} else {
			static_assert(
				texture_registry_detail::unsupported_type<Texture>,
				"TextureRegistry does not support this texture type"
			);
		}
	}

	template<typename Texture>
	Texture &load(
		const std::filesystem::path &path,
		gfx::TextureColorSpace color_space
	) {
		auto upload = gfx::UploadBatch::create(*m.device);
		Texture &result = load<Texture>(upload, path, color_space);
		upload.submit().wait();
		return result;
	}

	template<typename Texture>
	Texture &load(
		gfx::UploadBatch &upload,
		const std::filesystem::path &path,
		gfx::TextureColorSpace color_space
	) {
		if constexpr(std::is_same_v<Texture, gfx::Texture2D>) {
			return load_2d(upload, path, color_space);
		} else {
			static_assert(
				texture_registry_detail::unsupported_type<Texture>,
				"TextureRegistry can only load Texture2D files"
			);
		}
	}

	template<typename Texture>
	Texture *find_named(std::string_view name) {
		const std::string key(name);
		if constexpr(std::is_same_v<Texture, gfx::Texture2D>) {
			auto found = m.named_2d.find(key);
			return found == m.named_2d.end() ? nullptr : &found->second;
		} else if constexpr(std::is_same_v<Texture, gfx::Texture2DArray>) {
			auto found = m.named_2d_arrays.find(key);
			return found == m.named_2d_arrays.end() ? nullptr : &found->second;
		} else if constexpr(std::is_same_v<Texture, gfx::Texture3D>) {
			auto found = m.named_3d.find(key);
			return found == m.named_3d.end() ? nullptr : &found->second;
		} else if constexpr(std::is_same_v<Texture, gfx::TextureCube>) {
			auto found = m.named_cubes.find(key);
			return found == m.named_cubes.end() ? nullptr : &found->second;
		} else {
			static_assert(
				texture_registry_detail::unsupported_type<Texture>,
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
