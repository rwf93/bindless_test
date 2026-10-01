#include "gfx/command_list.h"

#include <stdexcept>
#include <vector>

#include "gfx/device.h"
#include "gfx/pipeline.h"

namespace gfx {

void CommandList::set_root_data(
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

void CommandList::set_pipeline(const Pipeline &pipeline) const {
	set_pipeline(pipeline.native(), m.bind_point);
}

void CommandList::set_pipeline(
	VkPipeline pipeline,
	VkPipelineBindPoint bind_point
) const {
	m.bind_point = bind_point;
	m.device->dispatch().cmdBindPipeline(m.command, m.bind_point, pipeline);
	const VkDescriptorSet descriptor_sets[] = {
		m.device->descriptor_set(),
		m.device->descriptor_set(),
	};
	m.device->dispatch().cmdBindDescriptorSets(
		m.command,
		m.bind_point,
		m.device->pipeline_layout(),
		0,
		2,
		descriptor_sets,
		0,
		nullptr
	);
}

void CommandList::trace_rays(
	const VkStridedDeviceAddressRegionKHR &raygen,
	const VkStridedDeviceAddressRegionKHR &miss,
	const VkStridedDeviceAddressRegionKHR &hit,
	const VkStridedDeviceAddressRegionKHR &callable,
	uint32_t width,
	uint32_t height,
	uint32_t depth
) const {
	if(m.bind_point != VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
		throw std::logic_error("CommandList::trace_rays: ray-tracing pipeline is not bound");
	m.device->dispatch().cmdTraceRaysKHR(
		m.command,
		&raygen,
		&miss,
		&hit,
		&callable,
		width,
		height,
		depth
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

void CommandList::begin_rendering(const RenderingDesc &rendering) const {
	if(rendering.width == 0 || rendering.height == 0 || rendering.layer_count == 0)
		throw std::invalid_argument("CommandList::begin_rendering: invalid render area");

	std::vector<VkRenderingAttachmentInfo> colors;
	colors.reserve(rendering.colors.size());
	for(const RenderingAttachment &attachment : rendering.colors) {
		if(!attachment.image.valid())
			throw std::invalid_argument("CommandList::begin_rendering: invalid color attachment");

		VkRenderingAttachmentInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		info.imageView = attachment.image.view;
		info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		info.loadOp = attachment.load == LoadOp::Load
			? VK_ATTACHMENT_LOAD_OP_LOAD
			: attachment.load == LoadOp::Clear
				? VK_ATTACHMENT_LOAD_OP_CLEAR
				: VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		info.storeOp = attachment.store == StoreOp::Store
			? VK_ATTACHMENT_STORE_OP_STORE
			: VK_ATTACHMENT_STORE_OP_DONT_CARE;
		info.clearValue.color.float32[0] = attachment.clear.color[0];
		info.clearValue.color.float32[1] = attachment.clear.color[1];
		info.clearValue.color.float32[2] = attachment.clear.color[2];
		info.clearValue.color.float32[3] = attachment.clear.color[3];
		colors.push_back(info);
	}

	VkRenderingAttachmentInfo depth = {};
	if(rendering.depth) {
		if(!rendering.depth->image.valid())
			throw std::invalid_argument("CommandList::begin_rendering: invalid depth attachment");

		depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depth.imageView = rendering.depth->image.view;
		depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		depth.loadOp = rendering.depth->load == LoadOp::Load
			? VK_ATTACHMENT_LOAD_OP_LOAD
			: rendering.depth->load == LoadOp::Clear
				? VK_ATTACHMENT_LOAD_OP_CLEAR
				: VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depth.storeOp = rendering.depth->store == StoreOp::Store
			? VK_ATTACHMENT_STORE_OP_STORE
			: VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depth.clearValue.depthStencil.depth = rendering.depth->clear.depth;
		depth.clearValue.depthStencil.stencil = rendering.depth->clear.stencil;
	}

	VkRenderingInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	info.renderArea.extent = {rendering.width, rendering.height};
	info.layerCount = rendering.layer_count;
	info.colorAttachmentCount = uint32_t(colors.size());
	info.pColorAttachments = colors.data();
	info.pDepthAttachment = rendering.depth ? &depth : nullptr;
	m.device->dispatch().cmdBeginRendering(m.command, &info);
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
