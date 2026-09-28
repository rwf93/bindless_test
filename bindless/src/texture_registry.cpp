#include "texture_registry.h"

#include <filesystem>
#include <stdexcept>

#include <spdlog/spdlog.h>

#include "gfx/create_utils.h"

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

TextureRegistry TextureRegistry::create(gfx::Device &device) {
	std::unordered_map<std::string, gfx::Texture2D> builtins;
	emplace_named(builtins, "missing", "Texture2D", [&] {
		return gfx::Texture2D::generate(
			device,
			gfx::Texture2DDesc{
				.width = 128,
				.height = 128,
				.format = VK_FORMAT_R8G8B8A8_UNORM,
				.name = "missing",
			},
			[](int x, int y) {
				return ((x / 16) + (y / 16)) % 2 == 0
					? 0x00ff0090u
					: 0x00000000u;
			}
		);
	});
	emplace_named(builtins, "black", "Texture2D", [&] {
		return gfx::Texture2D::generate(
			device,
			gfx::Texture2DDesc{
				.width = 1,
				.height = 1,
				.format = VK_FORMAT_R8G8B8A8_UNORM,
				.name = "black",
			},
			[](int, int) { return 0xff000000u; }
		);
	});
	emplace_named(builtins, "white", "Texture2D", [&] {
		return gfx::Texture2D::generate(
			device,
			gfx::Texture2DDesc{
				.width = 1,
				.height = 1,
				.format = VK_FORMAT_R8G8B8A8_UNORM,
				.name = "white",
			},
			[](int, int) { return 0xffffffffu; }
		);
	});
	return TextureRegistry(M{
		.device = &device,
		.named_2d = std::move(builtins),
	});
}

gfx::Texture2D &TextureRegistry::create_impl(
	std::type_identity<gfx::Texture2D>,
	const gfx::Texture2DDesc &desc,
	std::function<uint32_t(int x, int y)> function
) {
	return emplace_named(m.named_2d, desc.name, "Texture2D", [&] {
		return gfx::Texture2D::generate(*m.device, desc, function);
	});
}

gfx::Texture2D &TextureRegistry::create_impl(
	std::type_identity<gfx::Texture2D>,
	const gfx::Texture2DDesc &desc,
	std::span<const std::byte> data
) {
	return emplace_named(m.named_2d, desc.name, "Texture2D", [&] {
		return gfx::Texture2D::create(*m.device, desc, data);
	});
}

gfx::Texture2D &TextureRegistry::create_impl(
	std::type_identity<gfx::Texture2D>,
	const gfx::Texture2DDesc &desc
) {
	return emplace_named(m.named_2d, desc.name, "Texture2D", [&] {
		return gfx::Texture2D::create(*m.device, desc);
	});
}

gfx::Texture2DArray &TextureRegistry::create_impl(
	std::type_identity<gfx::Texture2DArray>,
	const gfx::Texture2DArrayDesc &desc
) {
	return emplace_named(
		m.named_2d_arrays,
		desc.name,
		"Texture2DArray",
		[&] {
			return gfx::Texture2DArray::create(*m.device, desc);
		}
	);
}

gfx::Texture3D &TextureRegistry::create_impl(
	std::type_identity<gfx::Texture3D>,
	const gfx::Texture3DDesc &desc,
	std::function<uint32_t(int x, int y, int z)> function
) {
	return emplace_named(m.named_3d, desc.name, "Texture3D", [&] {
		return gfx::Texture3D::generate(*m.device, desc, function);
	});
}

gfx::Texture3D &TextureRegistry::create_impl(
	std::type_identity<gfx::Texture3D>,
	const gfx::Texture3DDesc &desc,
	std::span<const std::byte> data
) {
	return emplace_named(m.named_3d, desc.name, "Texture3D", [&] {
		return gfx::Texture3D::create(*m.device, desc, data);
	});
}

gfx::Texture3D &TextureRegistry::create_impl(
	std::type_identity<gfx::Texture3D>,
	const gfx::Texture3DDesc &desc
) {
	return emplace_named(m.named_3d, desc.name, "Texture3D", [&] {
		return gfx::Texture3D::create(*m.device, desc);
	});
}

gfx::TextureCube &TextureRegistry::create_impl(
	std::type_identity<gfx::TextureCube>,
	const gfx::TextureCubeDesc &desc
) {
	return emplace_named(m.named_cubes, desc.name, "TextureCube", [&] {
		return gfx::TextureCube::create(*m.device, desc);
	});
}

gfx::Texture2D &TextureRegistry::load_2d(
	const std::filesystem::path &path,
	gfx::TextureColorSpace color_space
) {
	const std::filesystem::path resolved =
		std::filesystem::absolute(path).lexically_normal();
	std::string key = resolved.generic_string();
	key += color_space == gfx::TextureColorSpace::SRGB ? "|srgb" : "|linear";

	auto entry = m.cached_2d.try_emplace(
		key,
		with_result_of([&] {
			return gfx::STBTextureLoader::load(*m.device, resolved, color_space);
		})
	).first;
	return entry->second;
}
