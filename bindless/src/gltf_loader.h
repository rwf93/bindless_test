#pragma once

#include <string>

#include "model.h"

class VFS;
class MaterialRegistry;

class GLTFLoader {
public:
	GLTFLoader() = delete;

	// Load a raw glTF/GLB and immediately upload it as a renderable model.
	static Model load(VFS &vfs, const std::string &path);

	// Load a model TOML sidecar, resolving its glTF source, material slots,
	// default material, and model-space scale before uploading the model.
	static Model load_toml(
		VFS &vfs,
		MaterialRegistry &materials,
		const std::string &path
	);
};
