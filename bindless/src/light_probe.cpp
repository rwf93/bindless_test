#include "light_probe.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "texture_2d.h"
#include "texture_2d_array.h"
#include "texture_cube.h"
#include "texture_registry.h"

namespace {

uint32_t full_mip_count(uint32_t resolution) {
	uint32_t count = 1;
	while(resolution > 1) {
		resolution >>= 1;
		count++;
	}
	return count;
}

} // namespace

LightProbe LightProbe::create(
	TextureRegistry &textures,
	const std::string &name,
	uint32_t resolution,
	uint32_t irradiance_resolution
) {
	if(name.empty())
		throw std::invalid_argument("LightProbe::create: name must not be empty");
	if(resolution == 0 || irradiance_resolution == 0)
		throw std::invalid_argument("LightProbe::create: resolutions must be greater than zero");

	const std::string capture_name = name + "_capture";
	const std::string specular_name = name + "_specular";
	const std::string diffuse_name = name + "_diffuse";
	const std::string depth_name = name + "_depth";

	textures.create<TextureCube>(
		resolution,
		1,
		VK_FORMAT_R16G16B16A16_SFLOAT,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		capture_name
	);
	textures.create<TextureCube>(
		resolution,
		full_mip_count(resolution),
		VK_FORMAT_R16G16B16A16_SFLOAT,
		VK_IMAGE_USAGE_STORAGE_BIT,
		specular_name
	);
	textures.create<TextureCube>(
		irradiance_resolution,
		1,
		VK_FORMAT_R16G16B16A16_SFLOAT,
		VK_IMAGE_USAGE_STORAGE_BIT,
		diffuse_name
	);
	textures.create<Texture2DArray>(
		resolution,
		resolution,
		LIGHT_PROBE_FACE_COUNT,
		VK_FORMAT_D32_SFLOAT,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		depth_name
	);
	return LightProbe(M{
		.textures = &textures,
		.data = MultiBuffer<LightProbeData>::create(),
		.name = name,
		.capture_name = capture_name,
		.specular_name = specular_name,
		.diffuse_name = diffuse_name,
		.depth_name = depth_name,
	});
}

void LightProbe::prepare() {
	if(m.live_update)
		m.dirty = true;

	const glm::mat4 projection = glm::perspectiveRH_ZO(
		glm::radians(90.0f),
		1.0f,
		m.near_plane,
		m.far_plane
	);
	const std::array<glm::vec3, LIGHT_PROBE_FACE_COUNT> directions = {
		glm::vec3( 1.0f,  0.0f,  0.0f),
		glm::vec3(-1.0f,  0.0f,  0.0f),
		glm::vec3( 0.0f,  1.0f,  0.0f),
		glm::vec3( 0.0f, -1.0f,  0.0f),
		glm::vec3( 0.0f,  0.0f,  1.0f),
		glm::vec3( 0.0f,  0.0f, -1.0f),
	};
	const std::array<glm::vec3, LIGHT_PROBE_FACE_COUNT> up = {
		glm::vec3(0.0f, -1.0f,  0.0f),
		glm::vec3(0.0f, -1.0f,  0.0f),
		glm::vec3(0.0f,  0.0f,  1.0f),
		glm::vec3(0.0f,  0.0f, -1.0f),
		glm::vec3(0.0f, -1.0f,  0.0f),
		glm::vec3(0.0f, -1.0f,  0.0f),
	};

	for(uint32_t face = 0; face < LIGHT_PROBE_FACE_COUNT; face++) {
		m.data->view_projection[face] = projection * glm::lookAtRH(
			m.position,
			m.position + directions[face],
			up[face]
		);
	}
	m.data->position = glm::vec4(m.position, 1.0f);
}

TextureCube &LightProbe::capture() {
	return m.textures->at_named<TextureCube>(m.capture_name);
}

const TextureCube &LightProbe::capture() const {
	return m.textures->at_named<TextureCube>(m.capture_name);
}

TextureCube &LightProbe::specular() {
	return m.textures->at_named<TextureCube>(m.specular_name);
}

const TextureCube &LightProbe::specular() const {
	return m.textures->at_named<TextureCube>(m.specular_name);
}

TextureCube &LightProbe::diffuse() {
	return m.textures->at_named<TextureCube>(m.diffuse_name);
}

const TextureCube &LightProbe::diffuse() const {
	return m.textures->at_named<TextureCube>(m.diffuse_name);
}

Texture2DArray &LightProbe::depth() {
	return m.textures->at_named<Texture2DArray>(m.depth_name);
}

const Texture2DArray &LightProbe::depth() const {
	return m.textures->at_named<Texture2DArray>(m.depth_name);
}

void LightProbe::set_position(const glm::vec3 &position) {
	if(position == m.position)
		return;
	m.position = position;
	m.dirty = true;
	m.ready = false;
}

void LightProbe::set_box(const glm::vec3 &minimum, const glm::vec3 &maximum) {
	const glm::vec3 ordered_min = glm::min(minimum, maximum);
	const glm::vec3 ordered_max = glm::max(minimum, maximum);
	m.box_min = ordered_min;
	m.box_max = ordered_max;
}

void LightProbe::set_intensity(float intensity) {
	m.intensity = std::max(intensity, 0.0f);
}

void LightProbe::set_priority(float priority) {
	m.priority = std::max(priority, 0.0f);
}

LightProbeSet LightProbeSet::create(
	TextureRegistry &textures,
	const std::string &name,
	size_t capacity,
	uint32_t brdf_resolution
) {
	if(name.empty())
		throw std::invalid_argument("LightProbeSet::create: name must not be empty");
	if(capacity == 0)
		throw std::invalid_argument("LightProbeSet::create: capacity must be greater than zero");
	if(brdf_resolution == 0)
		throw std::invalid_argument("LightProbeSet::create: BRDF resolution must be greater than zero");

	const std::string brdf_name = name + "_brdf_lut";
	textures.create<Texture2D>(
		brdf_resolution,
		brdf_resolution,
		VK_FORMAT_R16G16B16A16_SFLOAT,
		VK_IMAGE_USAGE_STORAGE_BIT,
		brdf_name
	);

	std::vector<LightProbe> probes;
	probes.reserve(capacity);
	return LightProbeSet(M{
		.textures = &textures,
		.data = MultiBuffer<ReflectionProbeData>::create(capacity),
		.probes = std::move(probes),
		.capacity = capacity,
		.name = name,
		.brdf_name = brdf_name,
	});
}

LightProbe &LightProbeSet::add(
	const std::string &name,
	uint32_t resolution,
	uint32_t irradiance_resolution
) {
	if(m.probes.size() >= m.capacity) {
		throw std::runtime_error(
			"LightProbeSet::add: probe capacity exceeded for '" + m.name + "'"
		);
	}
	m.probes.emplace_back(with_result_of([&] {
		return LightProbe::create(
			*m.textures,
			name,
			resolution,
			irradiance_resolution
		);
	}));
	return m.probes.back();
}

void LightProbeSet::prepare() {
	for(auto &probe : m.probes)
		probe.prepare();

	m.scheduled_capture.reset();
	for(size_t offset = 0; offset < m.probes.size(); offset++) {
		const size_t index = (m.capture_cursor + offset) % m.probes.size();
		if(m.probes[index].needs_capture()) {
			m.scheduled_capture = index;
			break;
		}
	}

	for(size_t index = 0; index < m.probes.size(); index++) {
		auto &probe = m.probes[index];
		const bool available = probe.ready() || m.scheduled_capture == index;
		m.data[index] = ReflectionProbeData{
			.specular_handle = probe.specular().handle(),
			.diffuse_handle = probe.diffuse().handle(),
			.position = glm::vec4(probe.position(), 1.0f),
			.box_min = glm::vec4(probe.box_min(), 0.0f),
			.box_max = glm::vec4(probe.box_max(), 0.0f),
			.parameters = glm::vec4(
				float(probe.specular().mip_count() - 1),
				available ? probe.intensity() : 0.0f,
				probe.priority(),
				probe.global() ? 1.0f : 0.0f
			),
		};
	}
}

void LightProbeSet::mark_all_dirty() {
	for(auto &probe : m.probes)
		probe.mark_dirty();
}

bool LightProbeSet::should_capture(const LightProbe &probe) const {
	return m.scheduled_capture &&
		&m.probes[*m.scheduled_capture] == &probe;
}

void LightProbeSet::finish_capture(LightProbe &probe) {
	for(size_t index = 0; index < m.probes.size(); index++) {
		if(&m.probes[index] != &probe)
			continue;
		probe.finish_capture();
		m.capture_cursor = (index + 1) % m.probes.size();
		m.scheduled_capture.reset();
		return;
	}
	throw std::invalid_argument(
		"LightProbeSet::finish_capture: probe does not belong to '" + m.name + "'"
	);
}

Texture2D &LightProbeSet::brdf_lut() {
	return m.textures->at_named<Texture2D>(m.brdf_name);
}

const Texture2D &LightProbeSet::brdf_lut() const {
	return m.textures->at_named<Texture2D>(m.brdf_name);
}
