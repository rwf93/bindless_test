#pragma once

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

#include "shader_types.h"

inline constexpr uint32_t LOCAL_SHADOW_MAP_RESOLUTION = 2048;
inline constexpr float LOCAL_SHADOW_NEAR_PLANE = 0.1f;
inline constexpr float LOCAL_SHADOW_FALLBACK_FAR_PLANE = 1000.0f;

struct LocalShadowView {
	glm::mat4 view_projection = glm::mat4(1.0f);
	uint32_t layer = 0;
};

std::array<glm::mat4, LOCAL_SHADOW_FACE_COUNT> build_point_shadow_view_projections(
	const glm::vec3 &position,
	float near_plane,
	float far_plane
);

glm::mat4 build_spot_shadow_view_projection(
	const glm::vec3 &position,
	const glm::vec3 &direction,
	float outer_half_angle_radians,
	float near_plane,
	float far_plane
);
