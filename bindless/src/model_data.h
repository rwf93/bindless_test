#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "shader_types.h"

inline constexpr uint32_t no_material_slot = UINT32_MAX;

// CPU-side, renderer-independent representation produced by an asset loader.
struct ModelPrimitiveData {
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	uint32_t material_slot = no_material_slot;
};

// A model can draw the same primitive from multiple glTF nodes without
// duplicating its GPU geometry.
struct ModelDrawData {
	uint32_t primitive_index = 0;
	glm::mat4 local_transform = glm::mat4(1.0f);
};

struct ModelData {
	std::vector<ModelPrimitiveData> primitives;
	std::vector<ModelDrawData> draws;
	std::vector<std::string> material_slots;
	std::string source_path;
};
