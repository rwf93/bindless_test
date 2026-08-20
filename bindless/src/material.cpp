#include <spdlog/spdlog.h>
#include <tomlcpp.hpp>
#include <cstring>

#include "material.h"
#include "vfs.h"

std::unordered_map<std::string, Material> Material::material_cache;

Material Material::create_impl(Pipeline &pipeline, const MaterialParam *params, size_t count) {
	auto &layout = pipeline.material_layout();
	if(!layout.valid())
		spdlog::error("Material::create: pipeline has no valid Material layout");

	auto buffer = MaterialBuffer::create(layout.stride, 1);
	uint32_t index = buffer.alloc();
	uint8_t *slot = buffer[index];
	memset(slot, 0, layout.stride);

	// Pre-fill every *_handle field with UINT32_MAX so that texture handles
	// the TOML omits are detected by the shader as "not bound" rather than
	// silently pointing at bindless slot 0.
	for (const auto &f : layout.fields) {
		if (f.size == sizeof(uint32_t) &&
			f.name.size() > 7 &&
			f.name.compare(f.name.size() - 7, 7, "_handle") == 0)
		{
			*reinterpret_cast<uint32_t*>(slot + f.offset) = UINT32_MAX;
		}
	}

	for (size_t i = 0; i < count; i++) {
		const MaterialParam &param = params[i];
		const MaterialField *field = layout.find(param.name);
		if(!field) {
			spdlog::warn("Material::create: shader declares no field '{}' (layout stride {}); ignoring",
				param.name, layout.stride);
			continue;
		}
		size_t copy = param.size < field->size ? param.size : field->size;
		std::memcpy(slot + field->offset, param.storage.data(), copy);
	}

	return Material(M{
		.pipeline = &pipeline,
		.buffer = std::move(buffer),
		.index = index
	});
}

Material Material::create_from_toml(VFS &vfs, const std::string &path) {
	auto resolved = vfs.resolve(path);
	auto result = toml::parseFile(resolved.string());
	if(!result.table) {
		spdlog::error("Material::load_toml: failed to parse '{}': {}", resolved.string(), result.errmsg);
		return Material{};
	}

	auto &root = *result.table;

	auto [has_pipeline, pipeline_name] = root.getString("pipeline");
	if(!has_pipeline || pipeline_name.empty()) {
		spdlog::error("Material::load_toml: no 'pipeline' key in '{}'", resolved.string());
		return Material{};
	}

	auto pit = Pipeline::registry.find(pipeline_name);
	if(pit == Pipeline::registry.end()) {
		spdlog::error("Material::load_toml: pipeline '{}' not found", pipeline_name);
		return Material{};
	}

	auto toml_dir = resolved.parent_path();
	std::vector<MaterialParam> params;

	for(auto &key : root.keys()) {
		if(key == "pipeline") continue;

		auto [has_str, str_val] = root.getString(key);
		if(has_str) {
			// @-prefix: named texture reference (e.g. "@color")
			if(!str_val.empty() && str_val[0] == '@') {
				std::string res_name = str_val.substr(1);
				uint32_t handle = Texture2D::find_named(res_name);
				if(handle != UINT32_MAX) {
					params.push_back(MaterialParam::tex(key.c_str(), handle));
					spdlog::info("\t{}.{} = \"@{}\" -> handle {}", path, key, res_name, handle);
				} else {
					spdlog::error("\t{}.{} = \"@{}\" named texture not found", path, key, res_name);
				}
				continue;
			}
			// Resolve texture paths via the injected VFS. Alias paths
			// (e.g. "models/Foo/bar.png") walk the mount table; absolute
			// paths pass through. As a last resort, fall back to
			// TOML-relative so legacy TOMLs keep working.
			std::filesystem::path tex_path = vfs.resolve(str_val);
			if(!std::filesystem::exists(tex_path))
				tex_path = toml_dir / str_val;
			uint32_t handle = Texture2D::load_cached(tex_path);
			params.push_back(MaterialParam::tex(key.c_str(), handle));
			spdlog::info("\t{}.{} = \"{}\" -> handle {}", path, key, str_val, handle);
			continue;
		}

		auto [has_dbl, dbl_val] = root.getDouble(key);
		if(has_dbl) {
			float f = float(dbl_val);
			params.push_back(MaterialParam::raw(key.c_str(), &f, sizeof(f)));
			spdlog::info("\t{}.{} = {}", path, key, f);
			continue;
		}

		auto arr = root.getArray(key);
		if(arr && arr->kind() == 'v') {
			std::vector<float> floats;
			for(int j = 0; j < arr->size(); j++) {
				auto [ok, val] = arr->getDouble(j);
				if(ok) floats.push_back(float(val));
			}
			params.push_back(MaterialParam::raw(key.c_str(), floats.data(), floats.size() * sizeof(float)));
			spdlog::info("\t{}.{} = [{} float(s)]", path, key, floats.size());
			continue;
		}

		spdlog::warn("\t{}.{} has unsupported value type, skipping", path, key);
	}

	spdlog::info("Material::load_toml: loaded '{}' (pipeline '{}')", path, pipeline_name);
	return Material::create(pit->second, params);
}

Material &Material::load_toml(VFS &vfs, const std::string &path) {
	auto it = material_cache.find(path);
	if(it != material_cache.end())
		return it->second;

	auto mat = create_from_toml(vfs, path);
	return material_cache.emplace(path, std::move(mat)).first->second;
}

void Material::reload_all(VFS &vfs) {
	for(auto &[path, mat] : material_cache)
		mat = create_from_toml(vfs, path);
}
