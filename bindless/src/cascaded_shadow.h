#pragma once

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

#include "shader_types.h"

static constexpr uint32_t SHADOW_MAP_RESOLUTION = 2048;
static constexpr float SHADOW_DISTANCE = 250.0f;
struct CascadedShadowData {
	std::array<glm::mat4, SHADOW_CASCADE_COUNT> view_projection;
	glm::vec4 split_depths;
};

CascadedShadowData build_cascaded_shadow_data(
	const glm::vec3 &camera_position,
	const glm::mat4 &camera_rotation,
	float vertical_fov,
	float aspect_ratio,
	float near_plane,
	float far_plane,
	const glm::vec3 &direction_to_light,
	uint32_t shadow_map_resolution,
	float split_lambda = 0.9f
);
