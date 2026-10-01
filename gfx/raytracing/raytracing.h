#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "gfx/raw_buffer.h"

namespace gfx {

class CommandList;
class Device;
class SlangProgram;
class UploadBatch;

namespace raytracing {

struct TriangleGeometry {
	VkDeviceAddress vertex_address = 0;
	uint32_t vertex_count = 0;
	VkDeviceSize vertex_stride = 0;
	VkFormat vertex_format = VK_FORMAT_R32G32B32_SFLOAT;
	VkDeviceAddress index_address = 0;
	uint32_t index_count = 0;
	VkIndexType index_type = VK_INDEX_TYPE_UINT32;
	VkGeometryFlagsKHR flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
};

struct BottomLevelDesc {
	std::span<const TriangleGeometry> triangles;
	VkBuildAccelerationStructureFlagsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
};

struct TopLevelDesc {
	std::span<const VkAccelerationStructureInstanceKHR> instances;
	VkBuildAccelerationStructureFlagsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
};

class AccelerationStructure {
	struct M {
		Device *device = nullptr;
		std::optional<RawBuffer> storage;
		VkAccelerationStructureKHR structure = VK_NULL_HANDLE;
		VkDeviceAddress address = 0;
		uint32_t handle = UINT32_MAX;
	};

	M m;
	explicit AccelerationStructure(M m) : m(std::move(m)) {}
	void destroy();

public:
	~AccelerationStructure();

	AccelerationStructure(const AccelerationStructure &) = delete;
	AccelerationStructure &operator=(const AccelerationStructure &) = delete;
	AccelerationStructure(AccelerationStructure &&other) noexcept;
	AccelerationStructure &operator=(AccelerationStructure &&other) noexcept;

	static AccelerationStructure record_bottom_level(
		UploadBatch &batch,
		const BottomLevelDesc &desc
	);
	static AccelerationStructure record_top_level(
		UploadBatch &batch,
		const TopLevelDesc &desc
	);

	VkAccelerationStructureKHR native() const { return m.structure; }
	VkDeviceAddress device_address() const { return m.address; }
	uint32_t handle() const { return m.handle; }
};

struct HitGroupDesc {
	std::string closest_hit;
	std::string any_hit;
	std::string intersection;
};

struct PipelineDesc {
	std::vector<HitGroupDesc> hit_groups;
	uint32_t max_recursion_depth = 1;
};

class Pipeline {
	struct M {
		Device *device = nullptr;
		VkPipeline pipeline = VK_NULL_HANDLE;
		std::optional<RawBuffer> shader_binding_table;
		VkStridedDeviceAddressRegionKHR raygen_region{};
		VkStridedDeviceAddressRegionKHR miss_region{};
		VkStridedDeviceAddressRegionKHR hit_region{};
		VkStridedDeviceAddressRegionKHR callable_region{};
	};

	M m;
	explicit Pipeline(M m) : m(std::move(m)) {}
	void destroy();

public:
	~Pipeline();

	Pipeline(const Pipeline &) = delete;
	Pipeline &operator=(const Pipeline &) = delete;
	Pipeline(Pipeline &&other) noexcept;
	Pipeline &operator=(Pipeline &&other) noexcept;

	static Pipeline create(
		Device &device,
		SlangProgram &program,
		const PipelineDesc &desc
	);

	VkPipeline native() const { return m.pipeline; }
	void bind(CommandList &commands) const;
	void trace_rays(CommandList &commands, uint32_t width, uint32_t height = 1, uint32_t depth = 1) const;
};

} // namespace raytracing
} // namespace gfx
