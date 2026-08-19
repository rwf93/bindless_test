#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/common.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <fastgltf./core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <filesystem>
#include <vector>

#include "vkcontext.h"
#include "resource.h"
#include "material.h"
#include "vfs.h"

struct Vertex {
	glm::vec4 position;
	glm::vec4 texcoord;
	glm::vec4 normal;
};

struct SceneData {
	glm::vec4 camera_position;
	glm::mat4 projection;
	glm::mat4 view;
	uint32_t shadow_handle;
};

struct ObjectData {
	glm::mat4 model;
};

struct LightData {
	glm::vec4 position;
	glm::vec4 color;
};

class Mesh {
	struct M {
		GPUBuffer<Vertex> vbo;
		GPUBuffer<uint32_t> ibo;
		uint32_t material;
	} m;

	explicit Mesh(M m) : m(std::move(m)) {}
public:
	static Mesh create(std::vector<Vertex> &verts, std::vector<uint32_t> &index, uint32_t material_reference) {
		return Mesh(M{
			.vbo = std::move(GPUBuffer<Vertex>::create(verts)),
			.ibo = std::move(GPUBuffer<uint32_t>::create(index)),
			.material = std::move(material_reference)
		});
	}

	uint32_t material() { return m.material; }

	void draw(VkCommandBuffer cmd, PushConstants &push_constants, uint32_t instance_count, uint32_t first_instance) {
		push_constants.vbo_handle 		= m.vbo.handle();
		push_constants.ibo_handle 		= m.ibo.handle();

		device().cmdPushConstants(
			cmd,
			global_layout,
			VK_SHADER_STAGE_ALL,
			0,
			sizeof(PushConstants),
			&push_constants
		);
		device().cmdDraw(cmd, m.ibo.count(), instance_count, 0, first_instance);
	}
};

class Model {
	static constexpr size_t max_instances = 128;

	struct M {
		fastgltf::Expected<fastgltf::Asset> asset;
		std::vector<Mesh> meshes;
		SharedBuffer<ObjectData> mesh_transforms;
		size_t next_instance = 0;
	} m;

	explicit Model(M m) : m(std::move(m)) {}
public:
	static Model create(VFS &vfs, const std::string &path);

	// Draw the model with a single Material applied to all meshes. Each call
	// occupies its own contiguous block of transform slots (one per mesh).
	void draw(VkCommandBuffer cmd, PushConstants &constants, Material &material, const glm::mat4 &world = glm::mat4(1.0f)) {
		constants.object_handle = m.mesh_transforms.handle();

		uint32_t base = uint32_t(m.next_instance * m.meshes.size());
		assert(m.next_instance < max_instances);
		m.next_instance++;

		fastgltf::iterateSceneNodes(m.asset.get(), 0, fastgltf::math::fmat4x4(), [&](fastgltf::Node& node, fastgltf::math::fmat4x4 matrix) {
			uint32_t mesh_index = node.meshIndex.value();
			glm::mat4 node_matrix = world * glm::make_mat4(&matrix[0][0]);

			uint32_t slot = base + mesh_index;
			m.mesh_transforms[slot].model = node_matrix;

			material.bind(cmd, constants);
			m.meshes[mesh_index].draw(cmd, constants, 1, slot);
		});
	}

	// Clears the instance counter; call once per frame before drawing.
	void reset() { m.next_instance = 0; }

	Mesh &mesh(size_t idx) {
		return m.meshes.at(idx);
	}
};
