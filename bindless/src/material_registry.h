#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>

#include "material.h"

class PipelineRegistry;
class TextureRegistry;
class VFS;

class MaterialRegistry {
	struct M {
		VFS *vfs;
		PipelineRegistry *pipelines;
		TextureRegistry *textures;
		std::unordered_map<std::string, Material> materials;
	} m;

	explicit MaterialRegistry(M m) : m(std::move(m)) {}

	std::filesystem::path resolve(const std::filesystem::path &path) const;
	Material create_from_toml(const std::filesystem::path &path);

public:
	MaterialRegistry(MaterialRegistry &&) noexcept = default;
	MaterialRegistry &operator=(MaterialRegistry &&) noexcept = default;
	MaterialRegistry(const MaterialRegistry &) = delete;
	MaterialRegistry &operator=(const MaterialRegistry &) = delete;

	static MaterialRegistry create(
		VFS &vfs,
		PipelineRegistry &pipelines,
		TextureRegistry &textures
	);

	Material &load(const std::filesystem::path &path);

	// Replaces values inside existing map nodes, keeping every Material pointer
	// already held by a Model stable. A failed reload retains its old material.
	void reload_all();
};
