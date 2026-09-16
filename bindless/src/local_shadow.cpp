#include "local_shadow.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace {

glm::mat4 shadow_projection(float vertical_fov, float near_plane, float far_plane) {
	near_plane = std::max(near_plane, 0.001f);
	far_plane = std::max(far_plane, near_plane + 0.001f);

	auto projection = glm::perspectiveRH_ZO(vertical_fov, 1.0f, near_plane, far_plane);
	projection[1][1] *= -1.0f;
	return projection;
}

glm::vec3 normalized_or(const glm::vec3 &value, const glm::vec3 &fallback) {
	const float length_squared = glm::dot(value, value);
	return length_squared > 0.000001f
		? value / glm::sqrt(length_squared)
		: fallback;
}

} // namespace

std::array<glm::mat4, LOCAL_SHADOW_FACE_COUNT> build_point_shadow_view_projections(
	const glm::vec3 &position,
	float near_plane,
	float far_plane
) {
	const auto projection = shadow_projection(glm::radians(90.0f), near_plane, far_plane);
	const std::array<glm::vec3, LOCAL_SHADOW_FACE_COUNT> directions = {
		glm::vec3( 1.0f,  0.0f,  0.0f),
		glm::vec3(-1.0f,  0.0f,  0.0f),
		glm::vec3( 0.0f,  1.0f,  0.0f),
		glm::vec3( 0.0f, -1.0f,  0.0f),
		glm::vec3( 0.0f,  0.0f,  1.0f),
		glm::vec3( 0.0f,  0.0f, -1.0f),
	};
	const std::array<glm::vec3, LOCAL_SHADOW_FACE_COUNT> up = {
		glm::vec3(0.0f, -1.0f,  0.0f),
		glm::vec3(0.0f, -1.0f,  0.0f),
		glm::vec3(0.0f,  0.0f,  1.0f),
		glm::vec3(0.0f,  0.0f, -1.0f),
		glm::vec3(0.0f, -1.0f,  0.0f),
		glm::vec3(0.0f, -1.0f,  0.0f),
	};

	std::array<glm::mat4, LOCAL_SHADOW_FACE_COUNT> matrices;
	for(uint32_t face = 0; face < LOCAL_SHADOW_FACE_COUNT; face++)
		matrices[face] = projection * glm::lookAtRH(position, position + directions[face], up[face]);
	return matrices;
}

glm::mat4 build_spot_shadow_view_projection(
	const glm::vec3 &position,
	const glm::vec3 &direction,
	float outer_half_angle_radians,
	float near_plane,
	float far_plane
) {
	const glm::vec3 forward = normalized_or(direction, glm::vec3(0.0f, -1.0f, 0.0f));
	const glm::vec3 up = std::abs(forward.y) > 0.99f
		? glm::vec3(0.0f, 0.0f, 1.0f)
		: glm::vec3(0.0f, 1.0f, 0.0f);
	const float vertical_fov = glm::clamp(
		outer_half_angle_radians * 2.0f,
		glm::radians(1.0f),
		glm::radians(179.0f)
	);
	return shadow_projection(vertical_fov, near_plane, far_plane) *
		glm::lookAtRH(position, position + forward, up);
}
