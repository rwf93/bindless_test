#include "gltf_loader.h"

#include <cmath>
#include <filesystem>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>
#include <tomlcpp.hpp>

#include "material_registry.h"
#include "model_data.h"
#include "vfs.h"

namespace {

struct MeshRange {
	uint32_t first_primitive = 0;
	uint32_t primitive_count = 0;
};

struct ModelManifest {
	std::filesystem::path path;
	std::filesystem::path source;
	std::optional<std::filesystem::path> default_material;
	std::vector<std::filesystem::path> material_roots;
	std::unordered_map<std::string, std::filesystem::path> material_overrides;
	glm::vec3 scale = glm::vec3(1.0f);
};

std::filesystem::path resolve_reference(
	VFS &vfs,
	const std::filesystem::path &sidecar_directory,
	const std::string &reference
) {
	const std::filesystem::path virtual_path = vfs.resolve(reference);
	if(std::filesystem::exists(virtual_path))
		return std::filesystem::absolute(virtual_path).lexically_normal();

	const std::filesystem::path relative_path =
		(sidecar_directory / reference).lexically_normal();
	if(std::filesystem::exists(relative_path))
		return std::filesystem::absolute(relative_path).lexically_normal();

	// Preserve the VFS interpretation so a missing reference produces an
	// actionable absolute path in the eventual error.
	return std::filesystem::absolute(virtual_path).lexically_normal();
}

bool read_number(const toml::Table &table, const std::string &key, float &value) {
	auto [has_double, double_value] = table.getDouble(key);
	if(has_double) {
		value = float(double_value);
		return true;
	}

	auto [has_int, int_value] = table.getInt(key);
	if(has_int) {
		value = float(int_value);
		return true;
	}
	return false;
}

bool read_number(const toml::Array &array, int index, float &value) {
	auto [has_double, double_value] = array.getDouble(index);
	if(has_double) {
		value = float(double_value);
		return true;
	}

	auto [has_int, int_value] = array.getInt(index);
	if(has_int) {
		value = float(int_value);
		return true;
	}
	return false;
}

ModelManifest load_manifest(VFS &vfs, const std::string &path) {
	ModelManifest manifest;
	manifest.path = std::filesystem::absolute(vfs.resolve(path)).lexically_normal();
	if(!std::filesystem::exists(manifest.path)) {
		throw std::runtime_error(
			"GLTFLoader::load_toml: file does not exist: " +
			manifest.path.string()
		);
	}

	auto result = toml::parseFile(manifest.path.string());
	if(!result.table) {
		throw std::runtime_error(
			"GLTFLoader::load_toml: failed to parse '" +
			manifest.path.string() + "': " + result.errmsg
		);
	}

	auto &root = *result.table;
	const std::filesystem::path directory = manifest.path.parent_path();

	auto [has_source, source] = root.getString("source");
	if(!has_source || source.empty()) {
		throw std::runtime_error(
			"GLTFLoader::load_toml: no 'source': " + manifest.path.string()
		);
	}
	manifest.source = resolve_reference(vfs, directory, source);

	auto [has_default, default_material] = root.getString("default_material");
	if(has_default) {
		if(default_material.empty()) {
			throw std::runtime_error(
				"GLTFLoader::load_toml: default_material must not be empty: " +
				manifest.path.string()
			);
		}
		manifest.default_material = resolve_reference(
			vfs,
			directory,
			default_material
		);
	}

	if(auto roots = root.getArray("material_roots")) {
		for(int i = 0; i < roots->size(); i++) {
			auto [has_root, material_root] = roots->getString(i);
			if(!has_root || material_root.empty()) {
				throw std::runtime_error(
					"GLTFLoader::load_toml: material_roots must contain only "
					"non-empty strings: " + manifest.path.string()
				);
			}
			manifest.material_roots.push_back(resolve_reference(
				vfs,
				directory,
				material_root
			));
		}
	}

	if(auto overrides = root.getTable("material_overrides")) {
		for(const auto &slot_name : overrides->keys()) {
			auto [has_material, material] = overrides->getString(slot_name);
			if(!has_material || material.empty()) {
				throw std::runtime_error(
					"GLTFLoader::load_toml: material override '" + slot_name +
					"' must be a non-empty string: " + manifest.path.string()
				);
			}
			manifest.material_overrides.emplace(
				slot_name,
				resolve_reference(vfs, directory, material)
			);
		}
	}

	float uniform_scale = 1.0f;
	if(read_number(root, "scale", uniform_scale)) {
		manifest.scale = glm::vec3(uniform_scale);
	} else if(auto scale = root.getArray("scale")) {
		if(scale->size() != 3 ||
			!read_number(*scale, 0, manifest.scale.x) ||
			!read_number(*scale, 1, manifest.scale.y) ||
			!read_number(*scale, 2, manifest.scale.z))
		{
			throw std::runtime_error(
				"GLTFLoader::load_toml: scale must be one number or three "
				"numbers: " + manifest.path.string()
			);
		}
	}

	if(!std::isfinite(manifest.scale.x) ||
		!std::isfinite(manifest.scale.y) ||
		!std::isfinite(manifest.scale.z))
	{
		throw std::runtime_error(
			"GLTFLoader::load_toml: scale contains a non-finite value: " +
			manifest.path.string()
		);
	}

	return manifest;
}

Material *load_material(
	MaterialRegistry &materials,
	const std::filesystem::path &path
) {
	if(!std::filesystem::exists(path)) {
		throw std::runtime_error(
			"GLTFLoader::load_toml: material does not exist: " + path.string()
		);
	}

	return &materials.load(path);
}

ModelData load_data(VFS &vfs, const std::string &path) {
	static constexpr auto supported_extensions =
		fastgltf::Extensions::KHR_mesh_quantization |
		fastgltf::Extensions::KHR_texture_transform |
		fastgltf::Extensions::KHR_materials_variants;

	static constexpr auto options =
		fastgltf::Options::DontRequireValidAssetMember |
		fastgltf::Options::AllowDouble |
		fastgltf::Options::LoadExternalBuffers |
		fastgltf::Options::GenerateMeshIndices;

	const std::filesystem::path resolved =
		std::filesystem::absolute(vfs.resolve(path)).lexically_normal();
	if(!std::filesystem::exists(resolved)) {
		throw std::runtime_error(
			"GLTFLoader::load: source does not exist: " + resolved.string()
		);
	}
	if(resolved.extension() != ".gltf" && resolved.extension() != ".glb") {
		throw std::runtime_error(
			"GLTFLoader::load: expected a .gltf or .glb source: " +
			resolved.string()
		);
	}

	fastgltf::Parser parser(supported_extensions);
	auto gltf = fastgltf::MappedGltfFile::FromPath(resolved);
	if(gltf.error() != fastgltf::Error::None) {
		throw std::runtime_error(
			"GLTFLoader::load: failed to open '" + resolved.string() + "': " +
			std::string(fastgltf::getErrorMessage(gltf.error()))
		);
	}

	auto asset = resolved.extension() == ".glb"
		? parser.loadGltfBinary(gltf.get(), resolved.parent_path(), options)
		: parser.loadGltf(gltf.get(), resolved.parent_path(), options);
	if(asset.error() != fastgltf::Error::None) {
		throw std::runtime_error(
			"GLTFLoader::load: failed to parse '" + resolved.string() + "': " +
			std::string(fastgltf::getErrorMessage(asset.error()))
		);
	}

	ModelData data;
	data.source_path = resolved.string();
	data.material_slots.reserve(asset->materials.size());

	std::unordered_set<std::string> slot_names;
	for(size_t material_index = 0;
		material_index < asset->materials.size();
		material_index++)
	{
		std::string slot_name(asset->materials[material_index].name);
		if(slot_name.empty())
			slot_name = "material_" + std::to_string(material_index);

		if(!slot_names.insert(slot_name).second) {
			const std::string original_name = slot_name;
			slot_name += "_" + std::to_string(material_index);
			while(!slot_names.insert(slot_name).second)
				slot_name += "_";
			spdlog::warn(
				"GLTFLoader::load: duplicate material slot '{}' renamed to '{}' in '{}'",
				original_name,
				slot_name,
				resolved.string()
			);
		}

		data.material_slots.push_back(std::move(slot_name));
	}

	std::vector<MeshRange> mesh_ranges;
	mesh_ranges.reserve(asset->meshes.size());
	for(const auto &mesh : asset->meshes) {
		const uint32_t first_primitive = uint32_t(data.primitives.size());
		for(const auto &primitive : mesh.primitives) {
			if(primitive.type != fastgltf::PrimitiveType::Triangles) {
				throw std::runtime_error(
					"GLTFLoader::load: only triangle primitives are supported: " +
					resolved.string()
				);
			}

			const auto position_attribute = primitive.findAttribute("POSITION");
			if(position_attribute == primitive.attributes.end()) {
				throw std::runtime_error(
					"GLTFLoader::load: primitive has no POSITION attribute: " +
					resolved.string()
				);
			}

			const auto &position_accessor =
				asset->accessors.at(position_attribute->accessorIndex);
			ModelPrimitiveData primitive_data;
			primitive_data.vertices.resize(position_accessor.count);
			for(auto &vertex : primitive_data.vertices) {
				vertex.texcoord = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
				vertex.normal = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
			}

			fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
				asset.get(),
				position_accessor,
				[&](fastgltf::math::fvec3 position, size_t index) {
					primitive_data.vertices[index].position = glm::vec4(
						position.x(), position.y(), position.z(), 1.0f
					);
				}
			);

			const auto normal_attribute = primitive.findAttribute("NORMAL");
			if(normal_attribute != primitive.attributes.end()) {
				const auto &normal_accessor =
					asset->accessors.at(normal_attribute->accessorIndex);
				if(normal_accessor.count != primitive_data.vertices.size()) {
					throw std::runtime_error(
						"GLTFLoader::load: NORMAL count does not match POSITION count: " +
						resolved.string()
					);
				}
				fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
					asset.get(),
					normal_accessor,
					[&](fastgltf::math::fvec3 normal, size_t index) {
						primitive_data.vertices[index].normal = glm::vec4(
							normal.x(), normal.y(), normal.z(), 0.0f
						);
					}
				);
			}

			const auto texcoord_attribute = primitive.findAttribute("TEXCOORD_0");
			if(texcoord_attribute != primitive.attributes.end()) {
				const auto &texcoord_accessor =
					asset->accessors.at(texcoord_attribute->accessorIndex);
				if(texcoord_accessor.count != primitive_data.vertices.size()) {
					throw std::runtime_error(
						"GLTFLoader::load: TEXCOORD_0 count does not match POSITION count: " +
						resolved.string()
					);
				}
				fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
					asset.get(),
					texcoord_accessor,
					[&](fastgltf::math::fvec2 texcoord, size_t index) {
						primitive_data.vertices[index].texcoord = glm::vec4(
							texcoord.x(), texcoord.y(), 1.0f, 1.0f
						);
					}
				);
			}

			if(primitive.indicesAccessor.has_value()) {
				const auto &index_accessor =
					asset->accessors.at(primitive.indicesAccessor.value());
				primitive_data.indices.resize(index_accessor.count);
				fastgltf::iterateAccessorWithIndex<uint32_t>(
					asset.get(),
					index_accessor,
					[&](uint32_t value, size_t index) {
						primitive_data.indices[index] = value;
					}
				);
			} else {
				primitive_data.indices.resize(primitive_data.vertices.size());
				std::iota(
					primitive_data.indices.begin(),
					primitive_data.indices.end(),
					0u
				);
			}

			if(primitive.materialIndex.has_value()) {
				if(primitive.materialIndex.value() >= data.material_slots.size()) {
					throw std::runtime_error(
						"GLTFLoader::load: primitive material index is out of range: " +
						resolved.string()
					);
				}
				primitive_data.material_slot =
					uint32_t(primitive.materialIndex.value());
			}

			data.primitives.push_back(std::move(primitive_data));
		}

		mesh_ranges.push_back({
			.first_primitive = first_primitive,
			.primitive_count = uint32_t(data.primitives.size()) - first_primitive,
		});
	}

	if(asset->scenes.empty()) {
		throw std::runtime_error(
			"GLTFLoader::load: asset has no scenes: " + resolved.string()
		);
	}
	const size_t scene_index = asset->defaultScene.value_or(0);
	if(scene_index >= asset->scenes.size()) {
		throw std::runtime_error(
			"GLTFLoader::load: default scene index is out of range: " +
			resolved.string()
		);
	}

	fastgltf::iterateSceneNodes(
		asset.get(),
		scene_index,
		fastgltf::math::fmat4x4(),
		[&](fastgltf::Node &node, fastgltf::math::fmat4x4 transform) {
			if(!node.meshIndex.has_value())
				return;
			if(node.meshIndex.value() >= mesh_ranges.size()) {
				throw std::runtime_error(
					"GLTFLoader::load: node mesh index is out of range: " +
					resolved.string()
				);
			}

			const auto &range = mesh_ranges[node.meshIndex.value()];
			const glm::mat4 local_transform = glm::make_mat4(&transform[0][0]);
			for(uint32_t offset = 0; offset < range.primitive_count; offset++) {
				data.draws.push_back({
					.primitive_index = range.first_primitive + offset,
					.local_transform = local_transform,
				});
			}
		}
	);

	if(data.draws.empty()) {
		throw std::runtime_error(
			"GLTFLoader::load: scene contains no drawable primitives: " +
			resolved.string()
		);
	}

	return data;
}

} // namespace

Model GLTFLoader::load(VFS &vfs, const std::string &path) {
	return Model::create(load_data(vfs, path));
}

Model GLTFLoader::load_toml(
	VFS &vfs,
	MaterialRegistry &material_registry,
	const std::string &path
) {
	const ModelManifest manifest = load_manifest(vfs, path);
	ModelData data = load_data(vfs, manifest.source.string());

	Model::MaterialBindings materials;
	if(manifest.default_material.has_value()) {
		materials.default_material = load_material(
			material_registry,
			manifest.default_material.value()
		);
	}
	materials.slots.resize(data.material_slots.size());

	std::unordered_set<std::string> used_overrides;
	for(size_t slot_index = 0;
		slot_index < data.material_slots.size();
		slot_index++)
	{
		const std::string &slot_name = data.material_slots[slot_index];
		std::filesystem::path material_path;

		auto override = manifest.material_overrides.find(slot_name);
		if(override == manifest.material_overrides.end()) {
			// Numeric aliases remain useful for unnamed glTF materials.
			override = manifest.material_overrides.find(std::to_string(slot_index));
		}
		if(override != manifest.material_overrides.end()) {
			used_overrides.insert(override->first);
			material_path = override->second;
		} else {
			for(const auto &root : manifest.material_roots) {
				const std::filesystem::path candidate =
					root / (slot_name + ".toml");
				if(std::filesystem::exists(candidate)) {
					material_path = candidate;
					break;
				}
			}
		}

		if(!material_path.empty()) {
			materials.slots[slot_index] = load_material(
				material_registry,
				material_path
			);
		} else {
			materials.slots[slot_index] = materials.default_material;
			spdlog::warn(
				"GLTFLoader::load_toml: material slot '{}' in '{}' uses the default material",
				slot_name,
				manifest.path.string()
			);
		}

		if(!materials.slots[slot_index]) {
			throw std::runtime_error(
				"GLTFLoader::load_toml: unresolved material slot '" + slot_name +
				"' and no default_material was provided: " +
				manifest.path.string()
			);
		}
		spdlog::debug(
			"GLTFLoader::load_toml: slot {} '{}' resolved",
			slot_index,
			slot_name
		);
	}

	for(const auto &entry : manifest.material_overrides) {
		if(!used_overrides.contains(entry.first)) {
			spdlog::warn(
				"GLTFLoader::load_toml: material override '{}' does not match a slot in '{}'",
				entry.first,
				manifest.path.string()
			);
		}
	}

	if(!materials.default_material) {
		for(const auto &primitive : data.primitives) {
			if(primitive.material_slot == no_material_slot) {
				throw std::runtime_error(
					"GLTFLoader::load_toml: primitive without a material requires "
					"default_material: " + manifest.path.string()
				);
			}
		}
	}

	return Model::create(
		std::move(data),
		std::move(materials),
		glm::scale(glm::mat4(1.0f), manifest.scale)
	);
}
