#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <optional>

#include "vkcontext.h"

class FrameGraph {
public:
	struct Input {
		std::string name;
	};

	struct Output {
		std::string name;
		VkAttachmentLoadOp load_op;
		VkAttachmentStoreOp store_op;
		VkClearValue clear_value;
	};

	// Wrapper passed to each pass's execute callback. Provides the command
	// buffer plus convenience methods for common rendering operations so
	// passes don't need to reach into device() / global_layout directly.
	// Implicitly converts to VkCommandBuffer for API compat with existing
	// draw helpers (Model::draw, Material::bind, ImGui, etc.).
	struct RenderContext {
		VkCommandBuffer cmd;

		operator VkCommandBuffer() const { return cmd; }

		void push_constants(VkShaderStageFlags stages, const void *data, size_t size) {
			device().cmdPushConstants(cmd, global_layout, stages, 0, uint32_t(size), data);
		}

		void bind_pipeline(VkPipeline pipeline) {
			device().cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		}

		void draw(uint32_t vertex_count, uint32_t instance_count = 1,
		          uint32_t first_vertex = 0, uint32_t first_instance = 0) {
			device().cmdDraw(cmd, vertex_count, instance_count, first_vertex, first_instance);
		}
	};

	using ExecuteFn = std::function<void(RenderContext &)>;

	FrameGraph &add_resource(const std::string &name, AllocatedImage image, VkFormat format, VkExtent2D extent);
	FrameGraph &add_resource(const std::string &name, VkFormat format, VkExtent2D extent);

	// Update only the image/view for a resource whose format and extent don't
	// change (e.g. swapchain image).  The companion overload that also takes
	// format/extent is kept for cases where they actually change.
	FrameGraph &update_resource(const std::string &name, AllocatedImage image);
	FrameGraph &update_resource(const std::string &name, AllocatedImage image, VkFormat format, VkExtent2D extent);

	// Render pass: wraps execute in cmdBeginRendering / cmdEndRendering with
	// the pass's outputs as color/depth attachments.
	FrameGraph &add_render_pass(const std::string &name,
		std::vector<Input> inputs, std::vector<Output> outputs, ExecuteFn execute);

	// Non-render pass: compute / copy / barrier passes that don't need
	// rendering.  Inputs/outputs are still tracked for barrier scheduling.
	FrameGraph &add_pass(const std::string &name,
		std::vector<Input> inputs, std::vector<Output> outputs, ExecuteFn execute);

	FrameGraph &compile();
	void execute(VkCommandBuffer cmd);

private:
	struct Resource {
		std::string name;
		AllocatedImage image;
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkFormat format = VK_FORMAT_UNDEFINED;
		VkExtent2D extent = {0, 0};
	};

	struct Pass {
		std::string name;
		ExecuteFn execute;
		std::vector<Input> inputs;
		std::vector<Output> outputs;
		bool uses_rendering = false;
	};

	// --- Precomputed per-pass data (filled during compile(), read during execute()) ---

	struct CompiledBarrier {
		size_t resource_index;
		VkImageLayout target_layout;
		VkPipelineStageFlags2 src_stage;
		VkAccessFlags2 src_access;
		VkPipelineStageFlags2 dst_stage;
		VkAccessFlags2 dst_access;
	};

	struct CompiledAttachment {
		VkRenderingAttachmentInfoKHR info;
		size_t resource_index;
		bool is_depth = false;
	};

	struct CompiledPass {
		std::vector<CompiledBarrier> barriers;
		bool has_rendering = false;
		VkExtent2D render_extent = {0, 0};
		std::vector<CompiledAttachment> color_attachments;
		std::optional<CompiledAttachment> depth_attachment;
	};

	struct M {
		std::vector<Pass> passes;
		std::vector<Resource> resources;
		std::unordered_map<std::string, size_t> resource_by_name;
		std::vector<CompiledPass> compiled_passes;
		bool compiled = false;
	} m;

	explicit FrameGraph(M m) : m(std::move(m)) {}

public:
	static FrameGraph create() { return FrameGraph(M{}); }
};
