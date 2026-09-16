#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "gpu_buffer.h"
#include "shader_types.h"

class Texture2D;
class Texture2DArray;
class TextureCube;
class TextureRegistry;

inline constexpr uint32_t LIGHT_PROBE_DEFAULT_RESOLUTION = 128;
inline constexpr uint32_t LIGHT_PROBE_DEFAULT_IRRADIANCE_RESOLUTION = 16;
inline constexpr uint32_t LIGHT_PROBE_DEFAULT_BRDF_RESOLUTION = 256;

// A local, scene-captured IBL probe. The registry owns all image allocations;
// this object owns the capture state and the per-frame camera data used while
// rendering the six cube faces.
class LightProbe {
	struct M {
		TextureRegistry *textures = nullptr;
		MultiBuffer<LightProbeData> data;
		glm::vec3 position = glm::vec3(0.0f);
		glm::vec3 box_min = glm::vec3(-50.0f);
		glm::vec3 box_max = glm::vec3(50.0f);
		float intensity = 1.0f;
		float priority = 1.0f;
		float near_plane = 0.1f;
		float far_plane = 1000.0f;
		bool dirty = true;
		bool live_update = false;
		bool global = false;
		bool ready = false;
		std::string name;
		std::string capture_name;
		std::string specular_name;
		std::string diffuse_name;
		std::string depth_name;
	} m;

	explicit LightProbe(M m) : m(std::move(m)) {}

public:
	LightProbe(LightProbe &&) noexcept = default;
	LightProbe &operator=(LightProbe &&) noexcept = default;
	LightProbe(const LightProbe &) = delete;
	LightProbe &operator=(const LightProbe &) = delete;

	static LightProbe create(
		TextureRegistry &textures,
		const std::string &name,
		uint32_t resolution = LIGHT_PROBE_DEFAULT_RESOLUTION,
		uint32_t irradiance_resolution = LIGHT_PROBE_DEFAULT_IRRADIANCE_RESOLUTION
	);

	void prepare();

	TextureCube &capture();
	const TextureCube &capture() const;
	TextureCube &specular();
	const TextureCube &specular() const;
	TextureCube &diffuse();
	const TextureCube &diffuse() const;
	Texture2DArray &depth();
	const Texture2DArray &depth() const;

	uint32_t handle() const { return m.data.handle(); }
	VkBuffer buffer() const { return m.data.buffer(); }
	VkDeviceSize size_bytes() const { return m.data.size_bytes(); }

	const std::string &name() const { return m.name; }
	const glm::vec3 &position() const { return m.position; }
	const glm::vec3 &box_min() const { return m.box_min; }
	const glm::vec3 &box_max() const { return m.box_max; }
	float intensity() const { return m.intensity; }
	float priority() const { return m.priority; }
	bool live_update() const { return m.live_update; }
	bool global() const { return m.global; }
	bool ready() const { return m.ready; }
	bool needs_capture() const { return m.dirty; }

	void set_position(const glm::vec3 &position);
	void set_box(const glm::vec3 &minimum, const glm::vec3 &maximum);
	void set_intensity(float intensity);
	void set_priority(float priority);
	void set_live_update(bool enabled) { m.live_update = enabled; }
	void set_global(bool enabled) { m.global = enabled; }
	void mark_dirty() { m.dirty = true; }
	void finish_capture() {
		m.dirty = false;
		m.ready = true;
	}
};

// Owns a bounded collection of scene probes, their GPU sampling table, and
// the BRDF integration LUT shared by every probe.
class LightProbeSet {
	struct M {
		TextureRegistry *textures = nullptr;
		MultiBuffer<ReflectionProbeData> data;
		std::vector<LightProbe> probes;
		size_t capacity = 0;
		size_t capture_cursor = 0;
		std::optional<size_t> scheduled_capture;
		bool brdf_dirty = true;
		std::string name;
		std::string brdf_name;
	} m;

	explicit LightProbeSet(M m) : m(std::move(m)) {}

public:
	LightProbeSet(LightProbeSet &&) noexcept = default;
	LightProbeSet &operator=(LightProbeSet &&) noexcept = default;
	LightProbeSet(const LightProbeSet &) = delete;
	LightProbeSet &operator=(const LightProbeSet &) = delete;

	static LightProbeSet create(
		TextureRegistry &textures,
		const std::string &name,
		size_t capacity,
		uint32_t brdf_resolution = LIGHT_PROBE_DEFAULT_BRDF_RESOLUTION
	);

	LightProbe &add(
		const std::string &name,
		uint32_t resolution = LIGHT_PROBE_DEFAULT_RESOLUTION,
		uint32_t irradiance_resolution = LIGHT_PROBE_DEFAULT_IRRADIANCE_RESOLUTION
	);

	void prepare();
	void mark_all_dirty();
	bool should_capture(const LightProbe &probe) const;
	void finish_capture(LightProbe &probe);
	void invalidate_brdf_lut() { m.brdf_dirty = true; }
	void finish_brdf_lut() { m.brdf_dirty = false; }

	Texture2D &brdf_lut();
	const Texture2D &brdf_lut() const;
	std::span<LightProbe> probes() { return m.probes; }
	std::span<const LightProbe> probes() const { return m.probes; }
	LightProbe &operator[](size_t index) { return m.probes.at(index); }
	const LightProbe &operator[](size_t index) const { return m.probes.at(index); }
	size_t size() const { return m.probes.size(); }
	bool needs_brdf_lut() const { return m.brdf_dirty; }

	uint32_t handle() const { return m.data.handle(); }
	VkBuffer buffer() const { return m.data.buffer(); }
	VkDeviceSize size_bytes() const { return m.data.size_bytes(); }
	const std::string &name() const { return m.name; }
};
