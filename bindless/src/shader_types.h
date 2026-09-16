#pragma once

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

// CPU mirrors of shader-visible data. Keep GPU-facing layouts here instead of
// coupling models, lights, probes, and Vulkan context setup to one another.
inline constexpr uint32_t SHADOW_CASCADE_COUNT = 4;
inline constexpr uint32_t LOCAL_SHADOW_FACE_COUNT = 6;
inline constexpr uint32_t LOCAL_SHADOW_MAX_VIEW_COUNT = 64;
inline constexpr uint32_t LIGHT_PROBE_FACE_COUNT = 6;

static_assert(LOCAL_SHADOW_MAX_VIEW_COUNT % 16 == 0);
static_assert(LOCAL_SHADOW_MAX_VIEW_COUNT <= 256);

struct GPUDescriptorHandle {
	uint32_t index = UINT32_MAX;
	uint32_t auxiliary = 0;

	GPUDescriptorHandle &operator=(uint32_t descriptor_index) {
		index = descriptor_index;
		auxiliary = 0;
		return *this;
	}
};

struct PushConstants {
	GPUDescriptorHandle vbo_handle;
	GPUDescriptorHandle ibo_handle;
	GPUDescriptorHandle scene_handle;
	GPUDescriptorHandle object_handle;
	GPUDescriptorHandle light_handle;
	GPUDescriptorHandle material_handle;
	GPUDescriptorHandle probe_handle;
};

struct Vertex {
	glm::vec4 position;
	glm::vec4 texcoord;
	glm::vec4 normal;
};

struct LocalShadowMatrixData {
	glm::mat4 view_projection;
};

struct LightProbeData {
	std::array<glm::mat4, LIGHT_PROBE_FACE_COUNT> view_projection;
	glm::vec4 position;
};

struct ReflectionProbeData {
	GPUDescriptorHandle specular_handle;
	GPUDescriptorHandle diffuse_handle;
	glm::vec4 position;
	glm::vec4 box_min;
	glm::vec4 box_max;
	// x: maximum specular LOD, y: intensity, z: priority, w: global fallback
	glm::vec4 parameters;
};

struct SceneData {
	glm::vec4 camera_position;
	glm::mat4 projection;
	glm::mat4 view;
	std::array<glm::mat4, SHADOW_CASCADE_COUNT> shadow_view_projection;
	glm::vec4 shadow_split_depths;
	glm::vec4 shadow_light_direction;
	glm::vec4 shadow_light_color;
	GPUDescriptorHandle shadowmap_handle;
	uint32_t shadow_cascade_count;
	uint32_t shadowmap_resolution;
	GPUDescriptorHandle local_shadowmap_handle;
	uint32_t local_shadowmap_resolution;
	uint32_t light_count;
	GPUDescriptorHandle local_shadow_matrix_handle;
	glm::vec2 local_shadow_padding;
	GPUDescriptorHandle reflection_probe_handle;
	uint32_t reflection_probe_count;
	uint32_t reflection_probe_padding;
	GPUDescriptorHandle ibl_brdf_lut_handle;
	glm::vec2 ibl_padding;
};

struct ObjectData {
	glm::mat4 model;
	// Compact atlas-layer list used only by the local-shadow vertex shader.
	// Packing four byte-sized layers per uint keeps lookup O(1) per vertex.
	std::array<glm::uvec4, LOCAL_SHADOW_MAX_VIEW_COUNT / 16>
		local_shadow_layers = {};
};

enum class LightType : uint32_t {
	Point = 0,
	Spot = 1,
};

struct LightData {
	glm::vec4 position;
	glm::vec4 direction;
	glm::vec4 color;
	uint32_t type;
	float range;
	float inner_cone_cos;
	float outer_cone_cos;
	uint32_t casts_shadow;
	float shadow_bias;
	glm::vec2 shadow_padding;
};

struct IBLPrefilterConstants {
	GPUDescriptorHandle source_handle;
	GPUDescriptorHandle output_handle;
	uint32_t resolution;
	uint32_t sample_count;
	float roughness;
	uint32_t padding;
};

struct IBLIrradianceConstants {
	GPUDescriptorHandle source_handle;
	GPUDescriptorHandle output_handle;
	uint32_t resolution;
	uint32_t sample_count;
};

struct IBLBRDFConstants {
	GPUDescriptorHandle output_handle;
	uint32_t width;
	uint32_t height;
	uint32_t sample_count;
	uint32_t padding;
};

static_assert(sizeof(GPUDescriptorHandle) == 8);
static_assert(sizeof(PushConstants) == 56);
static_assert(sizeof(Vertex) == 48);
static_assert(sizeof(LocalShadowMatrixData) == 64);
static_assert(sizeof(LightProbeData) == 400);
static_assert(sizeof(ReflectionProbeData) == 80);
static_assert(sizeof(SceneData) == 528);
static_assert(sizeof(ObjectData) == 128);
static_assert(sizeof(LightData) == 80);
static_assert(sizeof(IBLPrefilterConstants) == 32);
static_assert(sizeof(IBLIrradianceConstants) == 24);
static_assert(sizeof(IBLBRDFConstants) == 24);
