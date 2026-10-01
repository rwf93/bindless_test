#pragma once

#include <string>

#include "model.h"

class VFS;
class MaterialRegistry;
namespace gfx { class Device; class UploadBatch; }

class GLTFLoader {
public:
	GLTFLoader() = delete;

	// Load a raw glTF/GLB and immediately upload it as a renderable model.
	static Model load(
		gfx::Device &device,
		VFS &vfs,
		const std::string &path
	);
	static Model load(
		gfx::UploadBatch &upload,
		VFS &vfs,
		const std::string &path
	);

	// Load a model TOML sidecar, resolving its glTF source, material slots,
	// default material, and model-space scale before uploading the model.
	static Model load_toml(
		VFS &vfs,
		MaterialRegistry &materials,
		const std::string &path
	);
	static Model load_toml(
		gfx::UploadBatch &upload,
		VFS &vfs,
		MaterialRegistry &materials,
		const std::string &path
	);
};
