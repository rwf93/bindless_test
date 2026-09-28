#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "gfx/buffer.h"
#include "material.h"
#include "model_data.h"
#include "local_shadow.h"
#include "shader_types.h"

class Model {
public:
	struct MaterialBindings {
		std::vector<Material *> slots;
		Material *default_material = nullptr;
	};

private:
	static constexpr size_t max_instances = 128;
	friend class SceneRenderer;

	class Mesh {
		struct M {
			gfx::Buffer<Vertex> vbo;
			gfx::Buffer<uint32_t> ibo;
		} m;

		explicit Mesh(M m) : m(std::move(m)) {}

	public:
		static Mesh create(
			gfx::Device &device,
			std::span<const Vertex> vertices,
			std::span<const uint32_t> indices
		) {
			return Mesh(M{
				.vbo = gfx::Buffer<Vertex>::create(device, vertices),
				.ibo = gfx::Buffer<uint32_t>::create(device, indices),
			});
		}

		void draw(
			gfx::CommandList &commands,
			PushConstants &push_constants,
			uint32_t instance_count,
			uint32_t first_instance
		) {
			push_constants.vbo_handle = m.vbo.handle();
			push_constants.ibo_handle = m.ibo.handle();
			commands.push_constants(&push_constants, sizeof(PushConstants));
			commands.draw(m.ibo.count(), instance_count, 0, first_instance);
		}
	};

	struct Primitive {
		Mesh mesh;
		uint32_t material_slot = no_material_slot;
		glm::vec3 bounds_min = glm::vec3(0.0f);
		glm::vec3 bounds_max = glm::vec3(0.0f);
	};

	struct M {
		std::vector<Primitive> primitives;
		std::vector<ModelDrawData> draws;
		std::vector<Material *> materials;
		Material *default_material = nullptr;
		gfx::MultiBuffer<ObjectData> mesh_transforms;
		glm::mat4 model_transform = glm::mat4(1.0f);
		std::string source_path;
		size_t next_instance = 0;
	} m;

	explicit Model(M m) : m(std::move(m)) {}

	Material &material_for(uint32_t slot) const;
	void draw_materials(
		gfx::CommandList &commands,
		PushConstants &constants,
		Material *override_material,
		const glm::mat4 &world,
		gfx::Pipeline *override_pipeline = nullptr,
		uint32_t view_count = 1
	);

	void draw_depth(
		gfx::CommandList &commands,
		PushConstants &constants,
		gfx::Pipeline &pipeline,
		uint32_t view_count,
		const glm::mat4 &world
	);

	void draw_local_shadow_views(
		gfx::CommandList &commands,
		PushConstants &constants,
		gfx::Pipeline &pipeline,
		std::span<const LocalShadowView> views,
		const glm::mat4 &world
	);

public:
	// Upload loader-produced CPU data. Model has no knowledge of glTF, TOML, or
	// the VFS; those policies live in their respective loaders.
	static Model create(
		gfx::Device &device,
		ModelData data,
		MaterialBindings materials = {},
		glm::mat4 model_transform = glm::mat4(1.0f)
	);

private:

	// Draw using the model TOML's primitive-to-material mapping.
	void draw(
		gfx::CommandList &commands,
		PushConstants &constants,
		const glm::mat4 &world = glm::mat4(1.0f)
	) {
		draw_materials(commands, constants, nullptr, world);
	}

	// Explicit whole-model override, retained for simple/debug models.
	void draw(
		gfx::CommandList &commands,
		PushConstants &constants,
		Material &material,
		const glm::mat4 &world = glm::mat4(1.0f)
	) {
		draw_materials(commands, constants, &material, world);
	}

	// Cascaded depth-only draw path. Each object is drawn once per cascade;
	// the shadow vertex shader decodes object and cascade from InstanceIndex.
	void draw_shadow(gfx::CommandList &commands, PushConstants &constants, gfx::Pipeline &pipeline, const glm::mat4 &world = glm::mat4(1.0f)) {
		draw_depth(commands, constants, pipeline, SHADOW_CASCADE_COUNT, world);
	}

	// Camera depth-only draw path. The matching color pipeline can use an
	// EQUAL depth test with depth writes disabled to avoid shading overdraw.
	void draw_depth_prepass(
		gfx::CommandList &commands,
		PushConstants &constants,
		gfx::Pipeline &pipeline,
		const glm::mat4 &world = glm::mat4(1.0f)
	) {
		draw_depth(commands, constants, pipeline, 1, world);
	}

	// Local-light depth path. Each point light consumes six atlas layers;
	// spot lights use only the first layer of their fixed six-layer slot.
	void draw_local_shadows(
		gfx::CommandList &commands,
		PushConstants &constants,
		gfx::Pipeline &pipeline,
		std::span<const LocalShadowView> views,
		const glm::mat4 &world = glm::mat4(1.0f)
	) {
		draw_local_shadow_views(commands, constants, pipeline, views, world);
	}

	// Render all material primitives into the six layers of a scene probe.
	// The capture pipeline has the same Material layout as the PBR pipeline but
	// intentionally excludes IBL to prevent recursive probe feedback.
	void draw_probe(
		gfx::CommandList &commands,
		PushConstants &constants,
		gfx::Pipeline &pipeline,
		const glm::mat4 &world = glm::mat4(1.0f)
	) {
		draw_materials(
			commands,
			constants,
			nullptr,
			world,
			&pipeline,
			LIGHT_PROBE_FACE_COUNT
		);
	}

	void draw_probe(
		gfx::CommandList &commands,
		PushConstants &constants,
		gfx::Pipeline &pipeline,
		Material &material,
		const glm::mat4 &world = glm::mat4(1.0f)
	) {
		draw_materials(
			commands,
			constants,
			&material,
			world,
			&pipeline,
			LIGHT_PROBE_FACE_COUNT
		);
	}

	// Clears the instance counter; call once per frame before drawing.
	void reset() { m.next_instance = 0; }

};
