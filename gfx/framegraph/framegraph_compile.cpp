#include "gfx/framegraph/framegraph.h"

#include <stdexcept>
#include <unordered_set>

namespace gfx {

namespace {

struct ImageAccessState {
	VkImageLayout layout;
	VkPipelineStageFlags2 stage;
	VkAccessFlags2 access;
};

struct BufferAccessState {
	VkPipelineStageFlags2 stage;
	VkAccessFlags2 access;
};

ImageAccessState access_state(
	FrameGraph::ImageAccess access,
	VkPipelineStageFlags2 shader_stage
) {
	using Access = FrameGraph::ImageAccess;
	switch(access) {
	case Access::SampledRead:
		return {
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			shader_stage,
			VK_ACCESS_2_SHADER_READ_BIT,
		};
	case Access::StorageRead:
		return {
			VK_IMAGE_LAYOUT_GENERAL,
			shader_stage,
			VK_ACCESS_2_SHADER_READ_BIT,
		};
	case Access::StorageWrite:
		return {
			VK_IMAGE_LAYOUT_GENERAL,
			shader_stage,
			VK_ACCESS_2_SHADER_WRITE_BIT,
		};
	case Access::StorageReadWrite:
		return {
			VK_IMAGE_LAYOUT_GENERAL,
			shader_stage,
			VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
		};
	case Access::TransferRead:
		return {
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_TRANSFER_READ_BIT,
		};
	case Access::TransferWrite:
		return {
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
		};
	}
	throw std::runtime_error("FrameGraph: unknown image access");
}

BufferAccessState access_state(
	FrameGraph::BufferAccess access,
	VkPipelineStageFlags2 shader_stage
) {
	using Access = FrameGraph::BufferAccess;
	switch(access) {
	case Access::StorageRead:
		return {shader_stage, VK_ACCESS_2_SHADER_READ_BIT};
	case Access::StorageWrite:
		return {shader_stage, VK_ACCESS_2_SHADER_WRITE_BIT};
	case Access::StorageReadWrite:
		return {
			shader_stage,
			VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
		};
	case Access::UniformRead:
		return {shader_stage, VK_ACCESS_2_UNIFORM_READ_BIT};
	case Access::IndirectRead:
		return {
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
		};
	case Access::VertexRead:
		return {
			VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
			VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
		};
	case Access::IndexRead:
		return {
			VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
			VK_ACCESS_2_INDEX_READ_BIT,
		};
	case Access::TransferRead:
		return {
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_TRANSFER_READ_BIT,
		};
	case Access::TransferWrite:
		return {
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
		};
	}
	throw std::runtime_error("FrameGraph: unknown buffer access");
}

} // namespace

FrameGraph &FrameGraph::compile() & {
	m.compiled_passes.clear();
	m.compiled_passes.reserve(m.passes.size());

	for(const auto &pass : m.passes) {
		CompiledPass compiled;
		compiled.shader_stage = pass.kind == PassKind::Compute
			? VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
			: pass.kind == PassKind::RayTracing
				? VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR
				: VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
		std::unordered_set<uint32_t> used_images;
		std::unordered_set<uint32_t> used_buffers;

		for(const auto &use : pass.images) {
			validate(use.handle);
			if(!used_images.insert(use.handle.index).second) {
				throw std::runtime_error(
					"FrameGraph: image '" + m.images[use.handle.index].name +
					"' is declared more than once in pass '" + pass.name + "'"
				);
			}
			const auto state = access_state(use.access, compiled.shader_stage);
			compiled.image_barriers.push_back({
				use.handle.index,
				{state.layout, state.stage, state.access},
			});
		}

		for(const auto &use : pass.buffers) {
			validate(use.handle);
			if(!used_buffers.insert(use.handle.index).second) {
				throw std::runtime_error(
					"FrameGraph: buffer '" + m.buffers[use.handle.index].name +
					"' is declared more than once in pass '" + pass.name + "'"
				);
			}
			const auto state = access_state(use.access, compiled.shader_stage);
			compiled.buffer_barriers.push_back({
				use.handle.index,
				{state.stage, state.access},
			});
		}

		std::unordered_set<uint32_t> used_acceleration_structures;
		for(const auto &use : pass.acceleration_structures) {
			if(use.index >= m.acceleration_structures.size())
				throw std::runtime_error("FrameGraph: invalid acceleration structure");
			if(!used_acceleration_structures.insert(use.index).second) {
				throw std::runtime_error(
					"FrameGraph: acceleration structure is declared more than once in pass '" +
					pass.name + "'"
				);
			}
			compiled.acceleration_structure_reads.push_back(use.index);
		}

		if(pass.kind == PassKind::Render) {
			if(pass.attachments.empty()) {
				throw std::runtime_error(
					"FrameGraph: render pass '" + pass.name +
					"' has no attachments"
				);
			}

			compiled.has_rendering = true;
			const auto &first_desc =
				m.images[pass.attachments.front().handle.index].desc;
			const auto first_extent = first_desc.extent;
			compiled.render_extent = {first_extent.width, first_extent.height};
			compiled.render_layer_count = first_desc.layer_count;

			for(const auto &attachment : pass.attachments) {
				validate(attachment.handle);
				const auto &resource = m.images[attachment.handle.index];
				const bool format_is_depth =
					(gfx::image_aspect_mask(resource.desc.format) &
						VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
				if(format_is_depth != attachment.is_depth) {
					throw std::runtime_error(
						"FrameGraph: attachment type does not match format for image '" +
						resource.name + "'"
					);
				}
				if(!used_images.insert(attachment.handle.index).second) {
					throw std::runtime_error(
						"FrameGraph: image '" + resource.name +
						"' is declared more than once in pass '" + pass.name + "'"
					);
				}
				if(resource.desc.extent.width != first_extent.width ||
					resource.desc.extent.height != first_extent.height ||
					resource.desc.layer_count != first_desc.layer_count)
				{
					throw std::runtime_error(
						"FrameGraph: attachment extents/layer counts differ in pass '" +
						pass.name + "'"
					);
				}

				ImageState target;
				if(attachment.is_depth) {
					target.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
					target.stage =
						VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
						VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
					target.access =
						VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
					if(attachment.ops.load_op == VK_ATTACHMENT_LOAD_OP_LOAD) {
						target.access |=
							VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
					}
				} else {
					target.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
					target.stage =
						VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
					target.access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
					if(attachment.ops.load_op == VK_ATTACHMENT_LOAD_OP_LOAD)
						target.access |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
				}
				compiled.image_barriers.push_back({
					attachment.handle.index,
					target,
				});

				VkRenderingAttachmentInfoKHR info = {};
				info.sType =
					VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
				info.imageView = resource.image.view;
				info.imageLayout = target.layout;
				info.resolveMode = VK_RESOLVE_MODE_NONE;
				info.loadOp = attachment.ops.load_op;
				info.storeOp = attachment.ops.store_op;
				info.clearValue = attachment.ops.clear_value;

				CompiledAttachment compiled_attachment{
					info,
					attachment.handle.index,
					attachment.is_depth,
				};
				if(attachment.is_depth) {
					if(compiled.depth_attachment) {
						throw std::runtime_error(
							"FrameGraph: render pass '" + pass.name +
							"' has multiple depth attachments"
						);
					}
					compiled.depth_attachment = compiled_attachment;
				} else {
					compiled.color_attachments.push_back(compiled_attachment);
				}
			}
		} else if(!pass.attachments.empty()) {
			throw std::runtime_error(
				"FrameGraph: non-render pass '" + pass.name +
				"' cannot have raster attachments"
			);
		}

		m.compiled_passes.push_back(std::move(compiled));
	}

	m.compiled = true;
	return *this;
}

FrameGraph &&FrameGraph::compile() && {
	static_cast<FrameGraph &>(*this).compile();
	return std::move(*this);
}

} // namespace gfx
