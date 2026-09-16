#include "material_registry.h"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include <spdlog/spdlog.h>
#include <tomlcpp.hpp>

#include "create_utils.h"
#include "pipeline_registry.h"
#include "texture_registry.h"
#include "vfs.h"

namespace {

TextureColorSpace parse_texture_color_space(const std::string &name) {
	if(name == "linear") return TextureColorSpace::Linear;
	if(name == "srgb") return TextureColorSpace::SRGB;
	throw std::runtime_error(
		"invalid texture color_space '" + name +
		"'; expected 'linear' or 'srgb'"
	);
}

std::string material_key(const std::filesystem::path &path) {
	return path.generic_string();
}

} // namespace

MaterialRegistry MaterialRegistry::create(
	VFS &vfs,
	PipelineRegistry &pipelines,
	TextureRegistry &textures
) {
	return MaterialRegistry(M{
		.vfs = &vfs,
		.pipelines = &pipelines,
		.textures = &textures,
		.materials = {},
	});
}

std::filesystem::path MaterialRegistry::resolve(
	const std::filesystem::path &path
) const {
	const auto resolved = path.is_absolute()
		? path
		: m.vfs->resolve(path.generic_string());
	return std::filesystem::absolute(resolved).lexically_normal();
}

Material MaterialRegistry::create_from_toml(
	const std::filesystem::path &resolved
) {
	auto result = toml::parseFile(resolved.string());
	if(!result.table) {
		throw std::runtime_error(
			"failed to parse '" + resolved.string() + "': " + result.errmsg
		);
	}

	auto &root = *result.table;
	auto [has_pipeline, pipeline_name] = root.getString("pipeline");
	if(!has_pipeline || pipeline_name.empty())
		throw std::runtime_error("material has no pipeline: " + resolved.string());

	Pipeline *pipeline = m.pipelines->find(pipeline_name);
	if(!pipeline) {
		throw std::runtime_error(
			"pipeline '" + pipeline_name + "' not found for material '" +
			resolved.string() + "'"
		);
	}

	const std::filesystem::path directory = resolved.parent_path();
	std::vector<MaterialParam> parameters;

	for(const auto &key : root.keys()) {
		if(key == "pipeline") continue;

		if(auto texture = root.getTable(key)) {
			auto [has_path, texture_reference] = texture->getString("path");
			auto [has_space, color_space_name] = texture->getString("color_space");
			if(!has_path || texture_reference.empty() || !has_space) {
				throw std::runtime_error(
					resolved.string() + ": " + key +
					" must specify non-empty path and color_space"
				);
			}

			const TextureColorSpace color_space =
				parse_texture_color_space(color_space_name);
			std::filesystem::path texture_path = m.vfs->resolve(texture_reference);
			if(!std::filesystem::exists(texture_path))
				texture_path = directory / texture_reference;

			Texture2D &loaded = m.textures->load<Texture2D>(
				texture_path,
				color_space
			);
			parameters.push_back(MaterialParam::tex(key.c_str(), loaded.handle()));
			spdlog::debug(
				"\t{}.{} = '{}' ({}) -> handle {}",
				resolved.string(),
				key,
				texture_reference,
				color_space_name,
				loaded.handle()
			);
			continue;
		}

		auto [has_string, string_value] = root.getString(key);
		if(has_string) {
			if(string_value.empty() || string_value.front() != '@') {
				throw std::runtime_error(
					resolved.string() + ": " + key +
					" file textures must use { path = '...', color_space = 'linear|srgb' }"
				);
			}

			const std::string texture_name = string_value.substr(1);
			Texture2D *texture = m.textures->find_named<Texture2D>(texture_name);
			if(!texture) {
				throw std::runtime_error(
					"named Texture2D '@" + texture_name + "' not found for '" +
					resolved.string() + "'"
				);
			}
			parameters.push_back(MaterialParam::tex(key.c_str(), texture->handle()));
			spdlog::debug(
				"\t{}.{} = '@{}' -> handle {}",
				resolved.string(),
				key,
				texture_name,
				texture->handle()
			);
			continue;
		}

		if(auto array = root.getArray(key); array && array->kind() == 'v') {
			std::vector<float> values;
			values.reserve(array->size());
			for(int index = 0; index < array->size(); index++) {
				auto [has_double, double_value] = array->getDouble(index);
				if(has_double) {
					values.push_back(float(double_value));
					continue;
				}
				auto [has_int, int_value] = array->getInt(index);
				if(!has_int) {
					throw std::runtime_error(
						resolved.string() + ": " + key +
						" arrays must contain only numbers"
					);
				}
				values.push_back(float(int_value));
			}
			parameters.push_back(MaterialParam::raw(
				key.c_str(),
				values.data(),
				values.size() * sizeof(float)
			));
			continue;
		}

		auto [has_double, double_value] = root.getDouble(key);
		if(has_double) {
			const float value = float(double_value);
			parameters.push_back(MaterialParam::raw(key.c_str(), &value, sizeof(value)));
			continue;
		}

		auto [has_int, int_value] = root.getInt(key);
		if(has_int) {
			if(key.ends_with("_handle")) {
				if(int_value < 0 || uint64_t(int_value) > std::numeric_limits<uint32_t>::max())
					throw std::runtime_error("descriptor handle is out of range: " + key);
				const uint32_t handle = uint32_t(int_value);
				parameters.push_back(MaterialParam::tex(key.c_str(), handle));
			} else {
				const float value = float(int_value);
				parameters.push_back(MaterialParam::raw(key.c_str(), &value, sizeof(value)));
			}
			continue;
		}

		throw std::runtime_error(
			resolved.string() + ": unsupported material value for " + key
		);
	}

	spdlog::info(
		"MaterialRegistry: loaded '{}' (pipeline '{}')",
		resolved.string(),
		pipeline_name
	);
	return Material::create(*pipeline, parameters);
}

Material &MaterialRegistry::load(const std::filesystem::path &path) {
	const std::filesystem::path resolved = resolve(path);
	const std::string key = material_key(resolved);
	return m.materials.try_emplace(
		key,
		with_result_of([&] {
			return create_from_toml(resolved);
		})
	).first->second;
}

void MaterialRegistry::reload_all() {
	device().deviceWaitIdle();
	for(auto &[path, material] : m.materials) {
		try {
			material = create_from_toml(path);
		} catch(const std::exception &error) {
			spdlog::error(
				"MaterialRegistry: retaining '{}' after reload failed: {}",
				path,
				error.what()
			);
		}
	}
}
