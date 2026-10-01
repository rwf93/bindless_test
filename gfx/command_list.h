#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <utility>

#include "gfx/rendering.h"

namespace gfx {

class Device;
class Pipeline;

class CommandList {
	struct M {
		Device *device = nullptr;
		VkCommandBuffer command = VK_NULL_HANDLE;
		mutable VkPipelineBindPoint bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
	} m;

	explicit CommandList(M m) : m(std::move(m)) {}

public:
	static CommandList create(
		Device &device,
		VkCommandBuffer command,
		VkPipelineBindPoint bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS
	) {
		return CommandList(M{
			.device = &device,
			.command = command,
			.bind_point = bind_point,
		});
	}

	VkCommandBuffer native() const { return m.command; }

	void set_root_data(const void *data, size_t size) const;
	void set_pipeline(const Pipeline &pipeline) const;
	void set_pipeline(VkPipeline pipeline, VkPipelineBindPoint bind_point) const;
	void draw(
		uint32_t vertex_count,
		uint32_t instance_count = 1,
		uint32_t first_vertex = 0,
		uint32_t first_instance = 0
	) const;
	void dispatch(
		uint32_t group_count_x,
		uint32_t group_count_y = 1,
		uint32_t group_count_z = 1
	) const;
	void trace_rays(
		const VkStridedDeviceAddressRegionKHR &raygen,
		const VkStridedDeviceAddressRegionKHR &miss,
		const VkStridedDeviceAddressRegionKHR &hit,
		const VkStridedDeviceAddressRegionKHR &callable,
		uint32_t width,
		uint32_t height = 1,
		uint32_t depth = 1
	) const;
	void viewport(uint32_t width, uint32_t height) const;
	void begin_rendering(const RenderingDesc &rendering) const;
	void end_rendering() const;
	void transition(
		VkImage image,
		VkImageLayout current_layout,
		VkImageLayout new_layout,
		VkImageAspectFlags aspect_mask
	) const;
};

} // namespace gfx
