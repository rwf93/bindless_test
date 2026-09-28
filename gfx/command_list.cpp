#include "gfx/command_list.h"

#include "gfx/device.h"

namespace gfx {

void CommandList::push_constants(
	const void *data,
	size_t size
) const {
	m.device->dispatch().cmdPushConstants(
		m.command,
		m.device->pipeline_layout(),
		VK_SHADER_STAGE_ALL,
		0,
		uint32_t(size),
		data
	);
}

void CommandList::push_handle(uint32_t handle, uint32_t offset) const {
	struct { uint32_t handle; uint32_t offset; } constants = { handle, offset };

	m.device->dispatch().cmdPushConstants(
		m.command,
		m.device->pipeline_layout(),
		VK_SHADER_STAGE_ALL,
		0,
		sizeof(constants),
		&constants
	);
}

void CommandList::bind_pipeline(VkPipeline pipeline) const {
	m.device->dispatch().cmdBindPipeline(m.command, m.bind_point, pipeline);
	const VkDescriptorSet descriptor_set = m.device->descriptor_set();
	m.device->dispatch().cmdBindDescriptorSets(
		m.command,
		m.bind_point,
		m.device->pipeline_layout(),
		0,
		1,
		&descriptor_set,
		0,
		nullptr
	);
}

void CommandList::draw(
	uint32_t vertex_count,
	uint32_t instance_count,
	uint32_t first_vertex,
	uint32_t first_instance
) const {
	m.device->dispatch().cmdDraw(
		m.command,
		vertex_count,
		instance_count,
		first_vertex,
		first_instance
	);
}

void CommandList::dispatch(
	uint32_t group_count_x,
	uint32_t group_count_y,
	uint32_t group_count_z
) const {
	m.device->dispatch().cmdDispatch(
		m.command,
		group_count_x,
		group_count_y,
		group_count_z
	);
}

void CommandList::viewport(uint32_t width, uint32_t height) const {
	const VkRect2D scissor = {{0, 0}, {width, height}};
	const VkViewport viewport = {
		0.0f,
		0.0f,
		float(width),
		float(height),
		0.0f,
		1.0f,
	};
	m.device->dispatch().cmdSetScissor(m.command, 0, 1, &scissor);
	m.device->dispatch().cmdSetViewport(m.command, 0, 1, &viewport);
}

void CommandList::begin_rendering(const VkRenderingInfo &rendering) const {
	m.device->dispatch().cmdBeginRendering(m.command, &rendering);
}

void CommandList::end_rendering() const {
	m.device->dispatch().cmdEndRendering(m.command);
}

void CommandList::transition(
	VkImage image,
	VkImageLayout current_layout,
	VkImageLayout new_layout,
	VkImageAspectFlags aspect_mask
) const {
	m.device->transition(
		m.command,
		image,
		current_layout,
		new_layout,
		aspect_mask
	);
}

} // namespace gfx
