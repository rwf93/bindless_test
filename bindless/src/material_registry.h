#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>

#include "material.h"

class PipelineRegistry;
class TextureRegistry;
class VFS;
namespace gfx { class Device; class UploadBatch; }

class MaterialRegistry {
	struct M {
		VFS *vfs;
		PipelineRegistry *pipelines;
		TextureRegistry *textures;
		std::unordered_map<std::string, Material> materials;
	} m;

	explicit MaterialRegistry(M m) : m(std::move(m)) {}

	std::filesystem::path resolve(const std::filesystem::path &path) const;
	Material create_from_toml(const std::filesystem::path &path, gfx::UploadBatch *upload = nullptr);

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
	gfx::Device &device() const;

	Material &load(const std::filesystem::path &path);
	Material &load(gfx::UploadBatch &upload, const std::filesystem::path &path);

	// Replaces values inside existing map nodes, keeping every Material pointer
	// already held by a Model stable. A failed reload retains its old material.
	void reload_all();
};
