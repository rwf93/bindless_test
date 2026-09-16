#include "model.h"

#include <array>
#include <cassert>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {

glm::vec4 matrix_row(const glm::mat4 &matrix, uint32_t row) {
	return {
		matrix[0][row],
		matrix[1][row],
		matrix[2][row],
		matrix[3][row],
	};
}

bool intersects_frustum(
	const glm::vec3 &bounds_min,
	const glm::vec3 &bounds_max,
	const glm::mat4 &world,
	const glm::mat4 &view_projection
) {
	const glm::vec3 local_center = (bounds_min + bounds_max) * 0.5f;
	const glm::vec3 local_extent = (bounds_max - bounds_min) * 0.5f;
	const glm::vec3 center = glm::vec3(world * glm::vec4(local_center, 1.0f));
	const glm::vec3 extent =
		glm::abs(glm::vec3(world[0])) * local_extent.x +
		glm::abs(glm::vec3(world[1])) * local_extent.y +
		glm::abs(glm::vec3(world[2])) * local_extent.z;

	const glm::vec4 row0 = matrix_row(view_projection, 0);
	const glm::vec4 row1 = matrix_row(view_projection, 1);
	const glm::vec4 row2 = matrix_row(view_projection, 2);
	const glm::vec4 row3 = matrix_row(view_projection, 3);
	// Vulkan's clip volume is -w <= x,y <= w and 0 <= z <= w.
	const std::array<glm::vec4, 6> planes = {
		row3 + row0,
		row3 - row0,
		row3 + row1,
		row3 - row1,
		row2,
		row3 - row2,
	};

	for(const glm::vec4 &plane : planes) {
		const glm::vec3 normal = glm::vec3(plane);
		const float radius = glm::dot(glm::abs(normal), extent);
		if(glm::dot(normal, center) + plane.w + radius < 0.0f)
			return false;
	}
	return true;
}

} // namespace

Model Model::create(
	ModelData data,
	MaterialBindings materials,
	glm::mat4 model_transform
) {
	if(data.draws.empty())
		throw std::runtime_error("Model::create: model has no draws: " + data.source_path);
	if(!materials.slots.empty() &&
		materials.slots.size() != data.material_slots.size())
	{
		throw std::runtime_error(
			"Model::create: material binding count does not match slot count: " +
			data.source_path
		);
	}

	std::vector<Primitive> primitives;
	primitives.reserve(data.primitives.size());
	for(auto &primitive : data.primitives) {
		if(primitive.vertices.empty() || primitive.indices.empty()) {
			throw std::runtime_error(
				"Model::create: primitive has no geometry: " + data.source_path
			);
		}
		if(primitive.material_slot != no_material_slot &&
			primitive.material_slot >= data.material_slots.size())
		{
			throw std::runtime_error(
				"Model::create: primitive material slot is out of range: " +
				data.source_path
			);
		}

		glm::vec3 bounds_min(std::numeric_limits<float>::max());
		glm::vec3 bounds_max(std::numeric_limits<float>::lowest());
		for(const Vertex &vertex : primitive.vertices) {
			bounds_min = glm::min(bounds_min, glm::vec3(vertex.position));
			bounds_max = glm::max(bounds_max, glm::vec3(vertex.position));
		}

		primitives.push_back({
			.mesh = Mesh::create(primitive.vertices, primitive.indices),
			.material_slot = primitive.material_slot,
			.bounds_min = bounds_min,
			.bounds_max = bounds_max,
		});
	}

	for(const auto &draw : data.draws) {
		if(draw.primitive_index >= primitives.size()) {
			throw std::runtime_error(
				"Model::create: draw primitive index is out of range: " +
				data.source_path
			);
		}
	}

	auto mesh_transforms = MultiBuffer<ObjectData>::create(
		data.draws.size() * max_instances
	);

	return Model(M{
		.primitives = std::move(primitives),
		.draws = std::move(data.draws),
		.materials = std::move(materials.slots),
		.default_material = materials.default_material,
		.mesh_transforms = std::move(mesh_transforms),
		.model_transform = model_transform,
		.source_path = std::move(data.source_path),
	});
}

Material &Model::material_for(uint32_t slot) const {
	if(slot != no_material_slot && slot < m.materials.size()) {
		if(Material *material = m.materials[slot])
			return *material;
	}

	if(m.default_material)
		return *m.default_material;

	throw std::runtime_error(
		"Model::draw: model has no resolved material for this primitive: " +
		m.source_path
	);
}

void Model::draw_materials(
	FrameGraph::PassContext &ctx,
	PushConstants &constants,
	Material *override_material,
	const glm::mat4 &world,
	Pipeline *override_pipeline,
	uint32_t view_count
) {
	assert(view_count > 0);
	constants.object_handle = m.mesh_transforms.handle();

	assert(m.next_instance < max_instances);
	const uint32_t base = uint32_t(m.next_instance * m.draws.size());
	m.next_instance++;
	Material *bound_material = nullptr;

	for(uint32_t draw_index = 0; draw_index < m.draws.size(); draw_index++) {
		const auto &draw = m.draws[draw_index];
		auto &primitive = m.primitives[draw.primitive_index];
		Material *material = override_material;
		if(!material)
			material = &material_for(primitive.material_slot);

		if(material != bound_material) {
			if(override_pipeline)
				material->bind(ctx, constants, *override_pipeline);
			else
				material->bind(ctx, constants);
			bound_material = material;
		}

		const uint32_t transform_slot = base + draw_index;
		m.mesh_transforms[transform_slot].model =
			world * m.model_transform * draw.local_transform;
		primitive.mesh.draw(
			ctx,
			constants,
			view_count,
			transform_slot * view_count
		);
	}
}

void Model::draw_depth(
	FrameGraph::PassContext &ctx,
	PushConstants &constants,
	Pipeline &pipeline,
	uint32_t view_count,
	const glm::mat4 &world
) {
	assert(view_count > 0);
	assert(m.next_instance < max_instances);

	constants.object_handle = m.mesh_transforms.handle();
	constants.material_handle = UINT32_MAX;
	ctx.bind_pipeline(pipeline.pipeline());

	const uint32_t base = uint32_t(m.next_instance * m.draws.size());
	m.next_instance++;
	for(uint32_t draw_index = 0; draw_index < m.draws.size(); draw_index++) {
		const auto &draw = m.draws[draw_index];
		auto &primitive = m.primitives[draw.primitive_index];
		const uint32_t transform_slot = base + draw_index;
		m.mesh_transforms[transform_slot].model =
			world * m.model_transform * draw.local_transform;
		primitive.mesh.draw(
			ctx,
			constants,
			view_count,
			transform_slot * view_count
		);
	}
}

void Model::draw_local_shadow_views(
	FrameGraph::PassContext &ctx,
	PushConstants &constants,
	Pipeline &pipeline,
	std::span<const LocalShadowView> views,
	const glm::mat4 &world
) {
	if(views.size() > LOCAL_SHADOW_MAX_VIEW_COUNT) {
		throw std::runtime_error(
			"Model::draw_local_shadows: local shadow view count exceeds " +
			std::to_string(LOCAL_SHADOW_MAX_VIEW_COUNT)
		);
	}

	assert(m.next_instance < max_instances);
	constants.object_handle = m.mesh_transforms.handle();
	constants.material_handle = UINT32_MAX;
	ctx.bind_pipeline(pipeline.pipeline());

	const uint32_t base = uint32_t(m.next_instance * m.draws.size());
	m.next_instance++;
	for(uint32_t draw_index = 0; draw_index < m.draws.size(); draw_index++) {
		const auto &draw = m.draws[draw_index];
		auto &primitive = m.primitives[draw.primitive_index];
		const uint32_t transform_slot = base + draw_index;
		const glm::mat4 model = world * m.model_transform * draw.local_transform;

		uint64_t view_mask = 0;
		std::array<
			uint32_t,
			LOCAL_SHADOW_MAX_VIEW_COUNT / 4
		> packed_layers = {};
		uint32_t visible_view_count = 0;
		for(const LocalShadowView &view : views) {
			if(view.layer >= LOCAL_SHADOW_MAX_VIEW_COUNT) {
				throw std::runtime_error(
					"Model::draw_local_shadows: atlas layer exceeds the view-mask capacity"
				);
			}
			const uint64_t layer_bit = uint64_t(1) << view.layer;
			if((view_mask & layer_bit) == 0 && intersects_frustum(
				primitive.bounds_min,
				primitive.bounds_max,
				model,
				view.view_projection
			)) {
				view_mask |= layer_bit;
				const uint32_t word = visible_view_count / 4;
				const uint32_t shift = (visible_view_count % 4) * 8;
				packed_layers[word] |= view.layer << shift;
				visible_view_count++;
			}
		}

		auto &object = m.mesh_transforms[transform_slot];
		object.model = model;
		for(uint32_t group = 0; group < object.local_shadow_layers.size(); group++) {
			object.local_shadow_layers[group] = glm::uvec4(
				packed_layers[group * 4 + 0],
				packed_layers[group * 4 + 1],
				packed_layers[group * 4 + 2],
				packed_layers[group * 4 + 3]
			);
		}

		if(visible_view_count == 0)
			continue;

		primitive.mesh.draw(
			ctx,
			constants,
			visible_view_count,
			transform_slot * LOCAL_SHADOW_MAX_VIEW_COUNT
		);
	}
}
