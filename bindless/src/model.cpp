#include <cassert>

#include "model.h"
#include "vktools.h"

Model Model::create(VFS &vfs, const std::string &path) {
	static constexpr auto supported_extensions =
		fastgltf::Extensions::KHR_mesh_quantization |
		fastgltf::Extensions::KHR_texture_transform |
		fastgltf::Extensions::KHR_materials_variants;

	static constexpr auto gltf_options = fastgltf::Options::DontRequireValidAssetMember |
        fastgltf::Options::AllowDouble |
        fastgltf::Options::LoadExternalBuffers |
       // fastgltf::Options::LoadExternalImages |
		fastgltf::Options::GenerateMeshIndices;

	auto resolved = vfs.resolve(path);

	fastgltf::Parser parser(supported_extensions);
	auto gltf = fastgltf::MappedGltfFile::FromPath(resolved);

	auto asset = resolved.extension() == "glb" ?
		parser.loadGltfBinary(gltf.get(), resolved.parent_path(), gltf_options) :
		parser.loadGltf(gltf.get(), resolved.parent_path(), gltf_options);

// Mesh paired with Material index
	std::vector<Mesh> meshes;
	for(auto &mesh : asset->meshes) {
		for(auto it = mesh.primitives.begin(); it != mesh.primitives.end(); ++it) {
			uint32_t material = 0;
			if(it->materialIndex.has_value())
				material = it->materialIndex.value();

			std::vector<glm::vec4> positions;
			{
				auto *position_it = it->findAttribute("POSITION");
				auto &position_accessor = asset->accessors[position_it->accessorIndex];
				positions.resize(position_accessor.count);
				if(!position_accessor.bufferViewIndex.has_value())
					continue;
				fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset.get(), position_accessor, [&](fastgltf::math::fvec3 pos, std::size_t idx) {
					positions[idx] = glm::vec4(pos.x(), pos.y(), pos.z(), 1);
				});
			}

			std::vector<glm::vec4> normals;
			{
				auto *normal_it = it->findAttribute("NORMAL");
				auto &normal_accessor = asset->accessors[normal_it->accessorIndex];
				normals.resize(normal_accessor.count);
				if(!normal_accessor.bufferViewIndex.has_value())
					continue;
				fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset.get(), normal_accessor, [&](fastgltf::math::fvec3 pos, std::size_t idx) {
					normals[idx] = glm::vec4(pos.x(), pos.y(), pos.z(), 1);
				});
			}

			std::vector<glm::vec4> texcoords;
			{
				auto *texcoord_it = it->findAttribute("TEXCOORD_0");
				auto &texcoord_accessor = asset->accessors[texcoord_it->accessorIndex];
				texcoords.resize(texcoord_accessor.count);
				if(!texcoord_accessor.bufferViewIndex.has_value())
					continue;
				fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(asset.get(), texcoord_accessor, [&](fastgltf::math::fvec2 pos, std::size_t idx) {
					texcoords[idx] = glm::vec4(pos.x(), pos.y(), 1, 1);
				});
			}

			std::vector<uint32_t> indicies;
			{
				auto& index_accessor = asset->accessors[it->indicesAccessor.value()];
        		if (!index_accessor.bufferViewIndex.has_value())
					continue;
				indicies.resize(index_accessor.count);
				fastgltf::iterateAccessorWithIndex<uint32_t>(asset.get(), index_accessor, [&](uint32_t val, size_t idx) {
					indicies[idx] = val;
				});
			}

			// why would they be different??!??!?!?!?!?
			assert(positions.size() == texcoords.size());
			assert(positions.size() == normals.size());

			std::vector<Vertex> verticies(positions.size());
			for(int i = 0; i < verticies.size(); i++) {
				verticies[i].position = positions[i];
				verticies[i].texcoord = texcoords[i];
				verticies[i].normal = normals[i];
			}

			meshes.emplace_back(with_result_of([&](){
				return Mesh::create(verticies, indicies, it->materialIndex.value());
			}));
		}
	}

	auto mesh_transforms = SharedBuffer<ObjectData>::create(meshes.size() * max_instances);

	return Model(M{
		.asset = std::move(asset),
		.meshes = std::move(meshes),
		.mesh_transforms = std::move(mesh_transforms)
	});
}
