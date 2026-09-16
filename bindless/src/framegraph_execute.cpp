#include "framegraph.h"

#include <stdexcept>
#include <vector>

#include <spdlog/spdlog.h>

namespace {

constexpr VkAccessFlags2 write_access_mask =
	VK_ACCESS_2_SHADER_WRITE_BIT |
	VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
	VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
	VK_ACCESS_2_TRANSFER_WRITE_BIT |
	VK_ACCESS_2_HOST_WRITE_BIT |
	VK_ACCESS_2_MEMORY_WRITE_BIT;

bool needs_barrier(VkAccessFlags2 source, VkAccessFlags2 destination) {
	return (source & write_access_mask) != 0 ||
		(destination & write_access_mask) != 0;
}

} // namespace

void FrameGraph::execute(VkCommandBuffer cmd) {
	if(!m.compiled) {
		spdlog::error("FrameGraph::execute: graph not compiled");
		return;
	}

	refresh_external_resources();

	for(size_t pass_index = 0; pass_index < m.passes.size(); pass_index++) {
		auto &pass = m.passes[pass_index];
		auto &compiled = m.compiled_passes[pass_index];
		if(pass.condition && !pass.condition())
			continue;

		VkDebugUtilsLabelEXT label = {};
		label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
		label.pLabelName = pass.name.c_str();
		label.color[3] = 1.0f;
		device().cmdBeginDebugUtilsLabelEXT(cmd, &label);

		std::vector<VkImageMemoryBarrier2> image_barriers;
		std::vector<VkBufferMemoryBarrier2> buffer_barriers;
		image_barriers.reserve(compiled.image_barriers.size());
		buffer_barriers.reserve(compiled.buffer_barriers.size());

		for(const auto &barrier : compiled.image_barriers) {
			auto &resource = m.images[barrier.resource_index];
			if(resource.image.image == VK_NULL_HANDLE) {
				throw std::runtime_error(
					"FrameGraph: image '" + resource.name +
					"' was not updated before execute()"
				);
			}

			const bool layout_change =
				resource.state.layout != barrier.target.layout;
			if(layout_change ||
				needs_barrier(resource.state.access, barrier.target.access))
			{
				VkImageMemoryBarrier2 vk_barrier = {};
				vk_barrier.sType =
					VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
				vk_barrier.srcStageMask = resource.state.stage;
				vk_barrier.srcAccessMask = resource.state.access;
				vk_barrier.dstStageMask = barrier.target.stage;
				vk_barrier.dstAccessMask = barrier.target.access;
				vk_barrier.oldLayout = resource.state.layout;
				vk_barrier.newLayout = barrier.target.layout;
				vk_barrier.image = resource.image.image;
				vk_barrier.subresourceRange = resource.image.subresources;

				if(resource.state.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
					vk_barrier.srcStageMask =
						VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
					vk_barrier.srcAccessMask = VK_ACCESS_2_NONE;
				}
				image_barriers.push_back(vk_barrier);
			}
			resource.state = barrier.target;
		}

		for(const auto &barrier : compiled.buffer_barriers) {
			auto &resource = m.buffers[barrier.resource_index];
			if(resource.buffer == VK_NULL_HANDLE) {
				throw std::runtime_error(
					"FrameGraph: buffer '" + resource.name +
					"' was not updated before execute()"
				);
			}

			if(needs_barrier(resource.state.access, barrier.target.access)) {
				VkBufferMemoryBarrier2 vk_barrier = {};
				vk_barrier.sType =
					VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
				vk_barrier.srcStageMask = resource.state.stage;
				vk_barrier.srcAccessMask = resource.state.access;
				vk_barrier.dstStageMask = barrier.target.stage;
				vk_barrier.dstAccessMask = barrier.target.access;
				vk_barrier.buffer = resource.buffer;
				vk_barrier.offset = 0;
				vk_barrier.size = resource.desc.size;
				buffer_barriers.push_back(vk_barrier);
			}
			resource.state = barrier.target;
		}

		if(!image_barriers.empty() || !buffer_barriers.empty()) {
			VkDependencyInfo dependency = {};
			dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			dependency.imageMemoryBarrierCount =
				uint32_t(image_barriers.size());
			dependency.pImageMemoryBarriers = image_barriers.data();
			dependency.bufferMemoryBarrierCount =
				uint32_t(buffer_barriers.size());
			dependency.pBufferMemoryBarriers = buffer_barriers.data();
			device().cmdPipelineBarrier2(cmd, &dependency);
		}

		if(compiled.has_rendering) {
			std::vector<VkRenderingAttachmentInfoKHR> colors;
			colors.reserve(compiled.color_attachments.size());
			for(auto &attachment : compiled.color_attachments) {
				attachment.info.imageView =
					m.images[attachment.resource_index].image.view;
				colors.push_back(attachment.info);
			}
			if(compiled.depth_attachment) {
				compiled.depth_attachment->info.imageView =
					m.images[compiled.depth_attachment->resource_index].image.view;
			}

			VkRenderingInfoKHR rendering = {};
			rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
			rendering.renderArea = {{0, 0}, compiled.render_extent};
			rendering.layerCount = compiled.render_layer_count;
			rendering.colorAttachmentCount = uint32_t(colors.size());
			rendering.pColorAttachments = colors.data();
			rendering.pDepthAttachment = compiled.depth_attachment
				? &compiled.depth_attachment->info
				: nullptr;
			device().cmdBeginRendering(cmd, &rendering);
		}

		auto context = PassContext::create(
			cmd,
			pass.kind == PassKind::Compute
				? VK_PIPELINE_BIND_POINT_COMPUTE
				: VK_PIPELINE_BIND_POINT_GRAPHICS
		);
		pass.execute(context);

		if(compiled.has_rendering)
			device().cmdEndRendering(cmd);
		device().cmdEndDebugUtilsLabelEXT(cmd);
	}
}
