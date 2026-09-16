#include "cascaded_shadow.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>

std::array<glm::vec3, 8> frustum_corners(
	const glm::vec3 &camera_position,
	const glm::mat4 &camera_rotation,
	float vertical_fov,
	float aspect_ratio,
	float near_depth,
	float far_depth
) {
	const glm::vec3 forward = glm::normalize(glm::vec3(camera_rotation * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
	const glm::vec3 right = glm::normalize(glm::vec3(camera_rotation * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
	const glm::vec3 up = glm::normalize(glm::vec3(camera_rotation * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));

	const float tangent = std::tan(vertical_fov * 0.5f);
	const float near_height = tangent * near_depth;
	const float near_width = near_height * aspect_ratio;
	const float far_height = tangent * far_depth;
	const float far_width = far_height * aspect_ratio;
	const glm::vec3 near_center = camera_position + forward * near_depth;
	const glm::vec3 far_center = camera_position + forward * far_depth;

	return {
		near_center - right * near_width - up * near_height,
		near_center + right * near_width - up * near_height,
		near_center + right * near_width + up * near_height,
		near_center - right * near_width + up * near_height,
		far_center - right * far_width - up * far_height,
		far_center + right * far_width - up * far_height,
		far_center + right * far_width + up * far_height,
		far_center - right * far_width + up * far_height,
	};
}
CascadedShadowData build_cascaded_shadow_data(
	const glm::vec3 &camera_position,
	const glm::mat4 &camera_rotation,
	float vertical_fov,
	float aspect_ratio,
	float near_plane,
	float far_plane,
	const glm::vec3 &direction_to_light,
	uint32_t shadow_map_resolution,
	float split_lambda
) {
	CascadedShadowData result{};
	split_lambda = std::clamp(split_lambda, 0.0f, 1.0f);

	for(uint32_t cascade = 0; cascade < SHADOW_CASCADE_COUNT; cascade++) {
		const float fraction = float(cascade + 1) / float(SHADOW_CASCADE_COUNT);
		const float logarithmic = near_plane * std::pow(far_plane / near_plane, fraction);
		const float uniform = near_plane + (far_plane - near_plane) * fraction;
		result.split_depths[cascade] = glm::mix(uniform, logarithmic, split_lambda);
	}

	const glm::vec3 light_direction = glm::normalize(direction_to_light);
	const glm::vec3 light_up = std::abs(glm::dot(light_direction, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.95f
		? glm::vec3(0.0f, 0.0f, 1.0f)
		: glm::vec3(0.0f, 1.0f, 0.0f);

	float cascade_near = near_plane;
	for(uint32_t cascade = 0; cascade < SHADOW_CASCADE_COUNT; cascade++) {
		const float cascade_far = result.split_depths[cascade];
		const auto corners = frustum_corners(
			camera_position,
			camera_rotation,
			vertical_fov,
			aspect_ratio,
			cascade_near,
			cascade_far
		);

		glm::vec3 center(0.0f);
		for(const auto &corner : corners)
			center += corner;
		center /= float(corners.size());

		float radius = 0.0f;
		for(const auto &corner : corners)
			radius = std::max(radius, glm::length(corner - center));
		// Quantizing the radius keeps tiny camera rotations from continuously
		// changing the projection scale.
		radius = std::ceil(radius * 16.0f) / 16.0f;

		const float caster_padding = std::max(50.0f, radius);
		const glm::vec3 light_position = center + light_direction * (radius + caster_padding);
		const glm::mat4 light_view = glm::lookAtRH(light_position, center, light_up);

		float min_z = std::numeric_limits<float>::max();
		float max_z = std::numeric_limits<float>::lowest();
		for(const auto &corner : corners) {
			const float z = (light_view * glm::vec4(corner, 1.0f)).z;
			min_z = std::min(min_z, z);
			max_z = std::max(max_z, z);
		}

		const float light_near = std::max(0.01f, -max_z - caster_padding);
		const float light_far = std::max(light_near + 1.0f, -min_z + caster_padding);
		glm::mat4 light_projection = glm::orthoRH_ZO(
			-radius,
			radius,
			-radius,
			radius,
			light_near,
			light_far
		);
		light_projection[1][1] *= -1.0f;

		// Snap the projected world origin to a shadow-map texel. This removes
		// most translation shimmer while the camera moves inside a cascade.
		const glm::mat4 unstabilized = light_projection * light_view;
		glm::vec2 shadow_origin = glm::vec2(unstabilized * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
		shadow_origin *= float(shadow_map_resolution) * 0.5f;
		const glm::vec2 rounded_origin = glm::round(shadow_origin);
		const glm::vec2 offset = (rounded_origin - shadow_origin) * (2.0f / float(shadow_map_resolution));
		light_projection[3][0] += offset.x;
		light_projection[3][1] += offset.y;

		result.view_projection[cascade] = light_projection * light_view;
		cascade_near = cascade_far;
	}

	return result;
}
