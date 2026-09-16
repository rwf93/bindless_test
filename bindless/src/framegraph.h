#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "vkcontext.h"

template<typename T>
concept FrameGraphImageResource = requires(const T &resource) {
	{ resource.image() } -> std::same_as<ImageRef>;
	{ resource.initial_layout() } -> std::same_as<VkImageLayout>;
};

template<typename T>
concept FrameGraphBufferResource = requires(const T &resource) {
	{ resource.buffer() } -> std::convertible_to<VkBuffer>;
	{ resource.size_bytes() } -> std::convertible_to<VkDeviceSize>;
};

class FrameGraph {
	struct Pass;

public:
	static constexpr uint32_t invalid_resource = std::numeric_limits<uint32_t>::max();

	struct ImageHandle {
		uint32_t index = invalid_resource;
		bool valid() const { return index != invalid_resource; }
	};

	struct BufferHandle {
		uint32_t index = invalid_resource;
		bool valid() const { return index != invalid_resource; }
	};

	struct ImageDesc {
		VkFormat format = VK_FORMAT_UNDEFINED;
		VkExtent3D extent = {0, 0, 1};
		uint32_t layer_count = 1;
		VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkPipelineStageFlags2 initial_stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		VkAccessFlags2 initial_access = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
	};

	struct BufferDesc {
		VkDeviceSize size = VK_WHOLE_SIZE;
		VkPipelineStageFlags2 initial_stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		VkAccessFlags2 initial_access = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
	};

	// Wrapper for images supplied by an external system and potentially changed
	// every frame, such as an acquired swapchain image.
	class ExternalImage {
		struct M {
			ImageId id = 0;
			std::string resource_name;
			ImageDesc desc;
			std::function<ImageRef()> resolve;
		} m;

		explicit ExternalImage(M m) : m(std::move(m)) {}

	public:
		static ExternalImage create(
			std::string name,
			ImageDesc desc,
			std::function<ImageRef()> resolve
		);
		ImageRef image() const;
		VkImageLayout initial_layout() const { return m.desc.initial_layout; }
		const std::string &name() const { return m.resource_name; }
	};

	enum class ImageAccess {
		SampledRead,
		StorageRead,
		StorageWrite,
		StorageReadWrite,
		TransferRead,
		TransferWrite,
	};

	enum class BufferAccess {
		StorageRead,
		StorageWrite,
		StorageReadWrite,
		UniformRead,
		IndirectRead,
		VertexRead,
		IndexRead,
		TransferRead,
		TransferWrite,
	};

	struct AttachmentOps {
		VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
		VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE;
		VkClearValue clear_value = {};
	};

	class PassContext {
		struct M {
			VkCommandBuffer cmd = VK_NULL_HANDLE;
			VkPipelineBindPoint bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
		} m;

		explicit PassContext(M m) : m(std::move(m)) {}

	public:
		static PassContext create(VkCommandBuffer cmd, VkPipelineBindPoint bind_point) {
			return PassContext(M{.cmd = cmd, .bind_point = bind_point});
		}

		operator VkCommandBuffer() const { return m.cmd; }
		void push_constants(VkShaderStageFlags stages, const void *data, size_t size);
		void bind_pipeline(VkPipeline pipeline);

		void draw(
			uint32_t vertex_count,
			uint32_t instance_count = 1,
			uint32_t first_vertex = 0,
			uint32_t first_instance = 0
		);
		void dispatch(uint32_t group_count_x, uint32_t group_count_y = 1, uint32_t group_count_z = 1);
	};

	using ExecuteFn = std::function<void(PassContext &)>;
	using PassConditionFn = std::function<bool()>;

	class PassBuilder {
		friend class FrameGraph;
	protected:
		struct M {
			Pass *pass = nullptr;
		} m;

		explicit PassBuilder(M m) : m(std::move(m)) {}
		Pass &pass() const { return *m.pass; }

		PassBuilder &use_image(
			ImageId identity,
			std::string name,
			ImageRef initial,
			std::function<ImageRef()> resolve,
			VkImageLayout initial_layout,
			bool reset_each_frame,
			ImageAccess access
		);
		PassBuilder &use_buffer(
			uint64_t identity,
			VkBuffer initial,
			VkDeviceSize size,
			std::function<VkBuffer()> resolve,
			bool reset_each_frame,
			BufferAccess access
		);

		template<typename ResourceT>
		static std::string resource_name(const ResourceT &resource) {
			if constexpr(requires { std::string(resource.name()); })
				return std::string(resource.name());
			return {};
		}

		template<typename ResourceT>
		static uint64_t resource_identity(const ResourceT &resource) {
			return uint64_t(reinterpret_cast<uintptr_t>(std::addressof(resource)));
		}

	public:
		PassBuilder &enabled_if(PassConditionFn condition);

		PassBuilder &use(ImageHandle image, ImageAccess access);
		PassBuilder &use(const ExternalImage &image, ImageAccess access);

		template<FrameGraphImageResource ImageT>
		PassBuilder &use(const ImageT &image, ImageAccess access) {
			const auto *resource = std::addressof(image);
			const auto initial = image.image();
			return use_image(
				initial.id,
				resource_name(image),
				initial,
				[resource] { return resource->image(); },
				image.initial_layout(),
				false,
				access
			);
		}

		PassBuilder &read(ImageHandle image, ImageAccess access = ImageAccess::SampledRead);
		PassBuilder &read(const ExternalImage &image, ImageAccess access = ImageAccess::SampledRead);

		template<FrameGraphImageResource ImageT>
		PassBuilder &read(const ImageT &image, ImageAccess access = ImageAccess::SampledRead) {
			return use(image, access);
		}

		PassBuilder &write(ImageHandle image, ImageAccess access = ImageAccess::StorageWrite);
		PassBuilder &write(const ExternalImage &image, ImageAccess access = ImageAccess::StorageWrite);

		template<FrameGraphImageResource ImageT>
		PassBuilder &write(const ImageT &image, ImageAccess access = ImageAccess::StorageWrite) {
			return use(image, access);
		}

		PassBuilder &read_write(ImageHandle image);
		PassBuilder &read_write(const ExternalImage &image);

		template<FrameGraphImageResource ImageT>
		PassBuilder &read_write(const ImageT &image) {
			return use(image, ImageAccess::StorageReadWrite);
		}

		PassBuilder &use(BufferHandle buffer, BufferAccess access);

		template<FrameGraphBufferResource BufferT>
		PassBuilder &use(const BufferT &buffer, BufferAccess access) {
			const auto *resource = std::addressof(buffer);
			return use_buffer(
				resource_identity(buffer),
				buffer.buffer(),
				buffer.size_bytes(),
				[resource] { return resource->buffer(); },
				true,
				access
			);
		}

		PassBuilder &read(BufferHandle buffer, BufferAccess access = BufferAccess::StorageRead);

		template<FrameGraphBufferResource BufferT>
		PassBuilder &read(const BufferT &buffer, BufferAccess access = BufferAccess::StorageRead) {
			return use(buffer, access);
		}

		PassBuilder &write(BufferHandle buffer, BufferAccess access = BufferAccess::StorageWrite);

		template<FrameGraphBufferResource BufferT>
		PassBuilder &write(const BufferT &buffer, BufferAccess access = BufferAccess::StorageWrite) {
			return use(buffer, access);
		}

		PassBuilder &read_write(BufferHandle buffer);

		template<FrameGraphBufferResource BufferT>
		PassBuilder &read_write(const BufferT &buffer) {
			return use(buffer, BufferAccess::StorageReadWrite);
		}
	};

	class RenderPassBuilder final : public PassBuilder {
		friend class FrameGraph;
		explicit RenderPassBuilder(M m) : PassBuilder(std::move(m)) {}

		RenderPassBuilder &attachment(
			ImageId identity,
			std::string name,
			ImageRef initial,
			std::function<ImageRef()> resolve,
			VkImageLayout initial_layout,
			bool reset_each_frame,
			AttachmentOps ops,
			bool is_depth
		);

		template<FrameGraphImageResource ImageT>
		RenderPassBuilder &resource_attachment(const ImageT &image, AttachmentOps ops, bool is_depth) {
			const auto *resource = std::addressof(image);
			if constexpr(requires(const ImageT &value) {
				{ value.attachment_view() } -> std::same_as<ImageRef>;
			}) {
				const auto initial = image.attachment_view();
				return attachment(
					initial.id,
					resource_name(image),
					initial,
					[resource] { return resource->attachment_view(); },
					image.initial_layout(),
					false,
					ops,
					is_depth
				);
			}

			const auto initial = image.image();
			return attachment(
				initial.id,
				resource_name(image),
				initial,
				[resource] { return resource->image(); },
				image.initial_layout(),
				false,
				ops,
				is_depth
			);
		}

	public:
		static RenderPassBuilder create(Pass &pass) {
			return RenderPassBuilder(M{.pass = std::addressof(pass)});
		}

		RenderPassBuilder &color(ImageHandle image, AttachmentOps ops = {});
		RenderPassBuilder &color(const ExternalImage &image, AttachmentOps ops = {});

		template<FrameGraphImageResource ImageT>
		RenderPassBuilder &color(const ImageT &image, AttachmentOps ops = {}) {
			return resource_attachment(image, ops, false);
		}

		RenderPassBuilder &depth(ImageHandle image, AttachmentOps ops = {});
		RenderPassBuilder &depth(const ExternalImage &image, AttachmentOps ops = {});

		template<FrameGraphImageResource ImageT>
		RenderPassBuilder &depth(const ImageT &image, AttachmentOps ops = {}) {
			return resource_attachment(image, ops, true);
		}

		RenderPassBuilder &clear_color(
			ImageHandle image,
			std::array<float, 4> color = {0.0f, 0.0f, 0.0f, 0.0f},
			VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE
		);

		template<typename ImageT>
		requires FrameGraphImageResource<ImageT>
		RenderPassBuilder &clear_color(
			const ImageT &image,
			std::array<float, 4> value = {0.0f, 0.0f, 0.0f, 0.0f},
			VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE
		) {
			AttachmentOps ops{.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR, .store_op = store_op};
			for(size_t i = 0; i < value.size(); i++)
				ops.clear_value.color.float32[i] = value[i];
			return color(image, ops);
		}

		RenderPassBuilder &clear_depth(
			ImageHandle image,
			float depth = 1.0f,
			uint32_t stencil = 0,
			VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE
		);

		template<typename ImageT>
		requires FrameGraphImageResource<ImageT>
		RenderPassBuilder &clear_depth(
			const ImageT &image,
			float value = 1.0f,
			uint32_t stencil = 0,
			VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE
		) {
			AttachmentOps ops{.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR, .store_op = store_op};
			ops.clear_value.depthStencil = {value, stencil};
			return depth(image, ops);
		}

		RenderPassBuilder &load_color(
			ImageHandle image,
			VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE
		);

		template<typename ImageT>
		requires FrameGraphImageResource<ImageT>
		RenderPassBuilder &load_color(
			const ImageT &image,
			VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE
		) {
			return color(image, AttachmentOps{.load_op = VK_ATTACHMENT_LOAD_OP_LOAD, .store_op = store_op});
		}

		RenderPassBuilder &load_depth(
			ImageHandle image,
			VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE
		);

		template<typename ImageT>
		requires FrameGraphImageResource<ImageT>
		RenderPassBuilder &load_depth(
			const ImageT &image,
			VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE
		) {
			return depth(image, AttachmentOps{.load_op = VK_ATTACHMENT_LOAD_OP_LOAD, .store_op = store_op});
		}
	};

	class ComputePassBuilder final : public PassBuilder {
		friend class FrameGraph;
		explicit ComputePassBuilder(M m) : PassBuilder(std::move(m)) {}

	public:
		static ComputePassBuilder create(Pass &pass) {
			return ComputePassBuilder(M{.pass = std::addressof(pass)});
		}
	};

	using RenderSetupFn = std::function<void(RenderPassBuilder &)>;
	using ComputeSetupFn = std::function<void(ComputePassBuilder &)>;

	FrameGraph &add_render_pass(const std::string &name, RenderSetupFn setup, ExecuteFn execute) &;
	FrameGraph &&add_render_pass(const std::string &name, RenderSetupFn setup, ExecuteFn execute) &&;
	FrameGraph &add_compute_pass(const std::string &name, ComputeSetupFn setup, ExecuteFn execute) &;
	FrameGraph &&add_compute_pass(const std::string &name, ComputeSetupFn setup, ExecuteFn execute) &&;

	FrameGraph &compile() &;
	FrameGraph &&compile() &&;
	void execute(VkCommandBuffer cmd);

	FrameGraph(const FrameGraph &) = delete;
	FrameGraph &operator=(const FrameGraph &) = delete;
	FrameGraph(FrameGraph &&) noexcept = default;
	FrameGraph &operator=(FrameGraph &&) noexcept = default;

	static FrameGraph create() { return FrameGraph(M{}); }

private:
	enum class PassKind { Render, Compute };

	struct ImageState {
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
		VkAccessFlags2 access = VK_ACCESS_2_NONE;
	};

	struct BufferState {
		VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
		VkAccessFlags2 access = VK_ACCESS_2_NONE;
	};

	struct ImageReference {
		ImageId identity = 0;
		std::string name;
		ImageRef initial;
		std::function<ImageRef()> resolve;
		VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		bool reset_each_frame = false;
	};

	struct BufferReference {
		uint64_t identity = 0;
		VkBuffer initial = VK_NULL_HANDLE;
		VkDeviceSize size = VK_WHOLE_SIZE;
		std::function<VkBuffer()> resolve;
		bool reset_each_frame = false;
	};

	struct ImageResource {
		std::string name;
		ImageRef image;
		ImageDesc desc;
		ImageState initial_state;
		ImageState state;
		std::function<ImageRef()> resolve;
		bool reset_each_frame = false;
	};

	struct BufferResource {
		std::string name;
		VkBuffer buffer = VK_NULL_HANDLE;
		BufferDesc desc;
		BufferState initial_state;
		BufferState state;
		std::function<VkBuffer()> resolve;
		bool reset_each_frame = false;
	};

	struct ImageUse {
		ImageHandle handle;
		ImageAccess access;
		std::optional<ImageReference> reference;
	};

	struct BufferUse {
		BufferHandle handle;
		BufferAccess access;
		std::optional<BufferReference> reference;
	};

	struct Attachment {
		ImageHandle handle;
		AttachmentOps ops;
		bool is_depth = false;
		std::optional<ImageReference> reference;
	};

	struct Pass {
		std::string name;
		PassKind kind = PassKind::Render;
		ExecuteFn execute;
		PassConditionFn condition;
		std::vector<ImageUse> images;
		std::vector<BufferUse> buffers;
		std::vector<Attachment> attachments;
	};

	struct CompiledImageBarrier {
		uint32_t resource_index = invalid_resource;
		ImageState target;
	};

	struct CompiledBufferBarrier {
		uint32_t resource_index = invalid_resource;
		BufferState target;
	};

	struct CompiledAttachment {
		VkRenderingAttachmentInfoKHR info = {};
		uint32_t resource_index = invalid_resource;
		bool is_depth = false;
	};

	struct CompiledPass {
		std::vector<CompiledImageBarrier> image_barriers;
		std::vector<CompiledBufferBarrier> buffer_barriers;
		bool has_rendering = false;
		VkExtent2D render_extent = {0, 0};
		uint32_t render_layer_count = 1;
		std::vector<CompiledAttachment> color_attachments;
		std::optional<CompiledAttachment> depth_attachment;
	};

	struct M {
		std::vector<Pass> passes;
		std::vector<ImageResource> images;
		std::vector<BufferResource> buffers;
		std::unordered_map<std::string, uint32_t> image_by_name;
		std::unordered_map<std::string, uint32_t> buffer_by_name;
		std::unordered_map<uint64_t, uint32_t> image_by_identity;
		std::unordered_map<uint64_t, uint32_t> buffer_by_identity;
		std::vector<CompiledPass> compiled_passes;
		bool compiled = false;
	} m;


	explicit FrameGraph(M m) : m(std::move(m)) {}

	void validate(ImageHandle handle) const;
	void validate(BufferHandle handle) const;
	ImageHandle resolve_image(ImageReference reference, bool attachment);
	BufferHandle resolve_buffer(BufferReference reference);
	void resolve_resources(Pass &pass);
	void refresh_external_resources();
};
