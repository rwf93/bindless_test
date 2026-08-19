#include <spdlog/spdlog.h>
#include <stdexcept>

#include "framegraph.h"
#include "vktools.h"

FrameGraph &FrameGraph::add_resource(const std::string &name, AllocatedImage image, VkFormat format, VkExtent2D extent) {
	size_t idx = m.resources.size();
	m.resources.push_back({name, image, VK_IMAGE_LAYOUT_UNDEFINED, format, extent});
	m.resource_by_name[name] = idx;
	return *this;
}

FrameGraph &FrameGraph::add_resource(const std::string &name, VkFormat format, VkExtent2D extent) {
	return add_resource(name, AllocatedImage{}, format, extent);
}

FrameGraph &FrameGraph::update_resource(const std::string &name, AllocatedImage image) {
	auto it = m.resource_by_name.find(name);
	if(it != m.resource_by_name.end())
		m.resources[it->second].image = image;
	return *this;
}

FrameGraph &FrameGraph::update_resource(const std::string &name, AllocatedImage image, VkFormat format, VkExtent2D extent) {
	auto it = m.resource_by_name.find(name);
	if(it != m.resource_by_name.end()) {
		auto &res = m.resources[it->second];
		res.image = image;
		res.format = format;
		res.extent = extent;
	}
	return *this;
}

// ---------------------------------------------------------------------------
// Pass registration
// ---------------------------------------------------------------------------

FrameGraph &FrameGraph::add_render_pass(const std::string &name,
	std::vector<Input> inputs, std::vector<Output> outputs, ExecuteFn execute)
{
	m.passes.push_back({name, std::move(execute), std::move(inputs), std::move(outputs), true});
	return *this;
}

FrameGraph &FrameGraph::add_pass(const std::string &name,
	std::vector<Input> inputs, std::vector<Output> outputs, ExecuteFn execute)
{
	m.passes.push_back({name, std::move(execute), std::move(inputs), std::move(outputs), false});
	return *this;
}


FrameGraph &FrameGraph::compile() {
	m.compiled_passes.clear();

	// Validate that every input/output references a registered resource.
	for(const auto &pass : m.passes) {
		for(const auto &input : pass.inputs) {
			if(m.resource_by_name.find(input.name) == m.resource_by_name.end())
				throw std::runtime_error("Input resource '" + input.name + "' not found for pass '" + pass.name + "'");
		}
		for(const auto &output : pass.outputs) {
			if(m.resource_by_name.find(output.name) == m.resource_by_name.end())
				throw std::runtime_error("Output resource '" + output.name + "' not found for pass '" + pass.name + "'");
		}
	}

	// Simulate layout transitions to precompute the full barrier list per pass.
	// All resources start at UNDEFINED; the simulation records what each pass
	// transitions and stores the barrier metadata.  At execute() time the same
	// sequence is replayed (with layouts reset to UNDEFINED first), so the
	// precomputed oldLayout values match the runtime state.
	std::vector<VkImageLayout> sim_layouts(m.resources.size(), VK_IMAGE_LAYOUT_UNDEFINED);

	for(const auto &pass : m.passes) {
		CompiledPass cp;

		// Input barriers: transition to SHADER_READ_ONLY_OPTIMAL.
		for(const auto &input : pass.inputs) {
			size_t idx = m.resource_by_name[input.name];
			if(sim_layouts[idx] != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
				cp.barriers.push_back({
					idx,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
					VK_ACCESS_2_MEMORY_WRITE_BIT,
					VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
					VK_ACCESS_2_SHADER_READ_BIT,
				});
				sim_layouts[idx] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
		}

		// Output barriers: transition to color/depth attachment layout.
		for(const auto &output : pass.outputs) {
			size_t idx = m.resource_by_name[output.name];
			bool is_depth = m.resources[idx].format == VK_FORMAT_D32_SFLOAT;
			VkImageLayout target = is_depth
				? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
				: VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			if(sim_layouts[idx] != target) {
				cp.barriers.push_back({
					idx,
					target,
					VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
					VK_ACCESS_2_MEMORY_READ_BIT,
					is_depth ? VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
					         : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
					is_depth ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
					         : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				});
				sim_layouts[idx] = target;
			}
		}

		// Precompute rendering info for render passes.
		if(pass.uses_rendering && !pass.outputs.empty()) {
			cp.has_rendering = true;
			cp.render_extent = m.resources[m.resource_by_name[pass.outputs[0].name]].extent;

			for(const auto &output : pass.outputs) {
				size_t idx = m.resource_by_name[output.name];
				const auto &res = m.resources[idx];
				bool is_depth = res.format == VK_FORMAT_D32_SFLOAT;
				VkImageLayout layout = is_depth
					? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
					: VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

				VkRenderingAttachmentInfoKHR att = {};
				att.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
				att.imageView = res.image.view;
				att.imageLayout = layout;
				att.resolveMode = VK_RESOLVE_MODE_NONE;
				att.resolveImageView = VK_NULL_HANDLE;
				att.resolveImageLayout = layout;
				att.loadOp = output.load_op;
				att.storeOp = output.store_op;
				att.clearValue = output.clear_value;

				CompiledAttachment catt{att, idx, is_depth};
				if(is_depth)
					cp.depth_attachment = catt;
				else
					cp.color_attachments.push_back(catt);
			}
		}

		m.compiled_passes.push_back(std::move(cp));
	}

	m.compiled = true;
	return *this;
}

// ---------------------------------------------------------------------------
// Execute — replay precomputed barriers + rendering, dispatch pass lambdas
// ---------------------------------------------------------------------------

void FrameGraph::execute(VkCommandBuffer cmd) {
	if(!m.compiled) {
		spdlog::error("FrameGraph::execute: graph not compiled");
		return;
	}

	RenderContext ctx{cmd};

	// Reset all resource layouts to UNDEFINED at the start of each frame.
	// oldLayout=UNDEFINED is always valid (means "discard previous contents"),
	// so the precomputed barriers remain correct regardless of what layout
	// the image was left in by the previous frame's post-execute transitions.
	for(auto &res : m.resources)
		res.layout = VK_IMAGE_LAYOUT_UNDEFINED;

	// Refresh image views in compiled attachments from current resource state.
	// Static resources get the same view; dynamic resources (swapchain) pick
	// up the new view set by update_resource().
	for(auto &cp : m.compiled_passes) {
		for(auto &att : cp.color_attachments)
			att.info.imageView = m.resources[att.resource_index].image.view;
		if(cp.depth_attachment)
			cp.depth_attachment->info.imageView = m.resources[cp.depth_attachment->resource_index].image.view;
	}

	for(size_t i = 0; i < m.passes.size(); i++) {
		auto &pass = m.passes[i];
		auto &cp = m.compiled_passes[i];

		// Build and submit barriers from precomputed metadata.
		if(!cp.barriers.empty()) {
			std::vector<VkImageMemoryBarrier2> barriers;
			barriers.reserve(cp.barriers.size());

			for(const auto &cb : cp.barriers) {
				auto &res = m.resources[cb.resource_index];
				if(res.layout == cb.target_layout)
					continue;  // already in target layout, skip redundant barrier

				bool is_depth = res.format == VK_FORMAT_D32_SFLOAT;
				VkImageMemoryBarrier2 b = {};
				b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
				b.srcStageMask = cb.src_stage;
				b.srcAccessMask = cb.src_access;
				b.dstStageMask = cb.dst_stage;
				b.dstAccessMask = cb.dst_access;
				b.oldLayout = res.layout;
				b.newLayout = cb.target_layout;
				b.image = res.image.image;
				b.subresourceRange = info::image_subresource_range(
					is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT);

				barriers.push_back(b);
				res.layout = cb.target_layout;
			}

			if(!barriers.empty()) {
				VkDependencyInfo dep = {};
				dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
				dep.imageMemoryBarrierCount = uint32_t(barriers.size());
				dep.pImageMemoryBarriers = barriers.data();
				device().cmdPipelineBarrier2(cmd, &dep);
			}
		}

		// Begin rendering (if this is a render pass).
		if(cp.has_rendering) {
			VkRenderingInfoKHR ri = {};
			ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
			ri.renderArea = {{0, 0}, {cp.render_extent.width, cp.render_extent.height}};
			ri.layerCount = 1;

			std::vector<VkRenderingAttachmentInfoKHR> color_atts;
			color_atts.reserve(cp.color_attachments.size());
			for(const auto &att : cp.color_attachments)
				color_atts.push_back(att.info);

			ri.colorAttachmentCount = uint32_t(color_atts.size());
			ri.pColorAttachments = color_atts.data();
			ri.pDepthAttachment = cp.depth_attachment ? &cp.depth_attachment->info : nullptr;

			VkDebugUtilsLabelEXT label = {};
			label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
			label.pLabelName = pass.name.c_str();
			label.color[3] = 1.0f;

			device().cmdBeginDebugUtilsLabelEXT(cmd, &label);
			device().cmdBeginRendering(cmd, &ri);
		}

		pass.execute(ctx);

		if(cp.has_rendering) {
			device().cmdEndRendering(cmd);
			device().cmdEndDebugUtilsLabelEXT(cmd);
		}
	}
}
