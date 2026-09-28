#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace gfx {

class Device;

class CommandList {
	struct M {
		Device *device = nullptr;
		VkCommandBuffer command = VK_NULL_HANDLE;
		VkPipelineBindPoint bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
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

	void push_constants(const void *data, size_t size) const;
	void push_handle(uint32_t handle, uint32_t offset = 0) const;
	void bind_pipeline(VkPipeline pipeline) const;
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
	void viewport(uint32_t width, uint32_t height) const;
	void begin_rendering(const VkRenderingInfo &rendering) const;
	void end_rendering() const;
	void transition(
		VkImage image,
		VkImageLayout current_layout,
		VkImageLayout new_layout,
		VkImageAspectFlags aspect_mask
	) const;
};

} // namespace gfx
