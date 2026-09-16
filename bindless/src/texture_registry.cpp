#include "texture_registry.h"

#include <filesystem>
#include <stdexcept>

#include <spdlog/spdlog.h>

#include "create_utils.h"

namespace {

template<typename Texture, typename Factory>
Texture &emplace_named(
	std::unordered_map<std::string, Texture> &textures,
	const std::string &name,
	std::string_view type_name,
	Factory &&factory
) {
	if(name.empty()) {
		throw std::invalid_argument(
			"TextureRegistry::create: named texture must have a non-empty name"
		);
	}

	auto [entry, inserted] = textures.try_emplace(
		name,
		with_result_of(std::forward<Factory>(factory))
	);
	if(!inserted) {
		throw std::runtime_error(
			"TextureRegistry::create: duplicate " + std::string(type_name) +
			" name: " + name
		);
	}

	spdlog::info(
		"TextureRegistry: registered {} '{}' (handle {})",
		type_name,
		name,
		entry->second.handle()
	);
	return entry->second;
}

} // namespace

TextureRegistry TextureRegistry::create() {
	return TextureRegistry(M{});
}

Texture2D &TextureRegistry::create_impl(
	std::type_identity<Texture2D>,
	uint32_t width,
	uint32_t height,
	VkFormat format,
	std::function<uint32_t(int x, int y)> function,
	const std::string &name
) {
	return emplace_named(m.named_2d, name, "Texture2D", [&] {
		return Texture2D::create(
			width,
			height,
			format,
			function,
			name
		);
	});
}

Texture2D &TextureRegistry::create_impl(
	std::type_identity<Texture2D>,
	uint32_t width,
	uint32_t height,
	VkFormat format,
	std::span<const std::byte> data,
	const std::string &name
) {
	return emplace_named(m.named_2d, name, "Texture2D", [&] {
		return Texture2D::create(width, height, format, data, name);
	});
}

Texture2D &TextureRegistry::create_impl(
	std::type_identity<Texture2D>,
	uint32_t width,
	uint32_t height,
	VkFormat format,
	VkImageUsageFlags usage,
	const std::string &name
) {
	return emplace_named(m.named_2d, name, "Texture2D", [&] {
		return Texture2D::create_empty(width, height, format, usage, name);
	});
}

Texture2DArray &TextureRegistry::create_impl(
	std::type_identity<Texture2DArray>,
	uint32_t width,
	uint32_t height,
	uint32_t layer_count,
	VkFormat format,
	VkImageUsageFlags usage,
	const std::string &name
) {
	return emplace_named(
		m.named_2d_arrays,
		name,
		"Texture2DArray",
		[&] {
			return Texture2DArray::create_empty(
				width,
				height,
				layer_count,
				format,
				usage,
				name
			);
		}
	);
}

Texture3D &TextureRegistry::create_impl(
	std::type_identity<Texture3D>,
	uint32_t width,
	uint32_t height,
	uint32_t depth,
	VkFormat format,
	std::function<uint32_t(int x, int y, int z)> function,
	const std::string &name
) {
	return emplace_named(m.named_3d, name, "Texture3D", [&] {
		return Texture3D::create(
			width,
			height,
			depth,
			format,
			function,
			name
		);
	});
}

Texture3D &TextureRegistry::create_impl(
	std::type_identity<Texture3D>,
	uint32_t width,
	uint32_t height,
	uint32_t depth,
	VkFormat format,
	std::span<const std::byte> data,
	const std::string &name
) {
	return emplace_named(m.named_3d, name, "Texture3D", [&] {
		return Texture3D::create(
			width,
			height,
			depth,
			format,
			data,
			name
		);
	});
}

Texture3D &TextureRegistry::create_impl(
	std::type_identity<Texture3D>,
	uint32_t width,
	uint32_t height,
	uint32_t depth,
	VkFormat format,
	VkImageUsageFlags usage,
	const std::string &name
) {
	return emplace_named(m.named_3d, name, "Texture3D", [&] {
		return Texture3D::create_empty(
			width,
			height,
			depth,
			format,
			usage,
			name
		);
	});
}

TextureCube &TextureRegistry::create_impl(
	std::type_identity<TextureCube>,
	uint32_t resolution,
	uint32_t mip_count,
	VkFormat format,
	VkImageUsageFlags usage,
	const std::string &name
) {
	return emplace_named(m.named_cubes, name, "TextureCube", [&] {
		return TextureCube::create_empty(
			resolution,
			mip_count,
			format,
			usage,
			name
		);
	});
}

Texture2D &TextureRegistry::load_2d(
	const std::filesystem::path &path,
	TextureColorSpace color_space
) {
	const std::filesystem::path resolved =
		std::filesystem::absolute(path).lexically_normal();
	std::string key = resolved.generic_string();
	key += color_space == TextureColorSpace::SRGB ? "|srgb" : "|linear";

	auto entry = m.cached_2d.try_emplace(
		key,
		with_result_of([&] {
			return Texture2D::load_file(resolved, color_space);
		})
	).first;
	return entry->second;
}
