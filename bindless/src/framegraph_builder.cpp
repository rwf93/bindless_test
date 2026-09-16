#include "framegraph.h"

#include <stdexcept>

FrameGraph::PassBuilder &FrameGraph::PassBuilder::enabled_if(
	PassConditionFn condition
) {
	if(!condition)
		throw std::invalid_argument("FrameGraph: pass condition cannot be empty");
	pass().condition = std::move(condition);
	return *this;
}

FrameGraph::PassBuilder &FrameGraph::PassBuilder::use(
	ImageHandle image,
	ImageAccess access
) {
	pass().images.push_back({image, access, std::nullopt});
	return *this;
}

FrameGraph::PassBuilder &FrameGraph::PassBuilder::use_image(
	ImageId identity,
	std::string name,
	ImageRef initial,
	std::function<ImageRef()> resolve,
	VkImageLayout initial_layout,
	bool reset_each_frame,
	ImageAccess access
) {
	pass().images.push_back({
		.handle = {},
		.access = access,
		.reference = ImageReference{
			.identity = identity,
			.name = std::move(name),
			.initial = initial,
			.resolve = std::move(resolve),
			.initial_layout = initial_layout,
			.reset_each_frame = reset_each_frame,
		},
	});
	return *this;
}

FrameGraph::PassBuilder &FrameGraph::PassBuilder::use(
	const ExternalImage &image,
	ImageAccess access
) {
	const auto *resource = std::addressof(image);
	const auto initial = image.image();
	return use_image(
		initial.id,
		image.name(),
		initial,
		[resource] { return resource->image(); },
		image.initial_layout(),
		true,
		access
	);
}

FrameGraph::PassBuilder &FrameGraph::PassBuilder::read(
	ImageHandle image,
	ImageAccess access
) {
	return use(image, access);
}

FrameGraph::PassBuilder &FrameGraph::PassBuilder::read(
	const ExternalImage &image,
	ImageAccess access
) {
	return use(image, access);
}

FrameGraph::PassBuilder &FrameGraph::PassBuilder::write(
	ImageHandle image,
	ImageAccess access
) {
	return use(image, access);
}

FrameGraph::PassBuilder &FrameGraph::PassBuilder::write(
	const ExternalImage &image,
	ImageAccess access
) {
	return use(image, access);
}

FrameGraph::PassBuilder &FrameGraph::PassBuilder::read_write(
	ImageHandle image
) {
	return use(image, ImageAccess::StorageReadWrite);
}

FrameGraph::PassBuilder &FrameGraph::PassBuilder::read_write(
	const ExternalImage &image
) {
	return use(image, ImageAccess::StorageReadWrite);
}

FrameGraph::PassBuilder &FrameGraph::PassBuilder::use(
	BufferHandle buffer,
	BufferAccess access
) {
	pass().buffers.push_back({buffer, access, std::nullopt});
	return *this;
}

FrameGraph::PassBuilder &FrameGraph::PassBuilder::use_buffer(
	uint64_t identity,
	VkBuffer initial,
	VkDeviceSize size,
	std::function<VkBuffer()> resolve,
	bool reset_each_frame,
	BufferAccess access
) {
	pass().buffers.push_back({
		.handle = {},
		.access = access,
		.reference = BufferReference{
			.identity = identity,
			.initial = initial,
			.size = size,
			.resolve = std::move(resolve),
			.reset_each_frame = reset_each_frame,
		},
	});
	return *this;
}

FrameGraph::PassBuilder &FrameGraph::PassBuilder::read(
	BufferHandle buffer,
	BufferAccess access
) {
	return use(buffer, access);
}

FrameGraph::PassBuilder &FrameGraph::PassBuilder::write(
	BufferHandle buffer,
	BufferAccess access
) {
	return use(buffer, access);
}

FrameGraph::PassBuilder &FrameGraph::PassBuilder::read_write(
	BufferHandle buffer
) {
	return use(buffer, BufferAccess::StorageReadWrite);
}

FrameGraph::RenderPassBuilder &FrameGraph::RenderPassBuilder::color(
	ImageHandle image,
	AttachmentOps ops
) {
	pass().attachments.push_back({image, ops, false, std::nullopt});
	return *this;
}

FrameGraph::RenderPassBuilder &FrameGraph::RenderPassBuilder::depth(
	ImageHandle image,
	AttachmentOps ops
) {
	pass().attachments.push_back({image, ops, true, std::nullopt});
	return *this;
}

FrameGraph::RenderPassBuilder &FrameGraph::RenderPassBuilder::attachment(
	ImageId identity,
	std::string name,
	ImageRef initial,
	std::function<ImageRef()> resolve,
	VkImageLayout initial_layout,
	bool reset_each_frame,
	AttachmentOps ops,
	bool is_depth
) {
	pass().attachments.push_back({
		.handle = {},
		.ops = ops,
		.is_depth = is_depth,
		.reference = ImageReference{
			.identity = identity,
			.name = std::move(name),
			.initial = initial,
			.resolve = std::move(resolve),
			.initial_layout = initial_layout,
			.reset_each_frame = reset_each_frame,
		},
	});
	return *this;
}

FrameGraph::RenderPassBuilder &FrameGraph::RenderPassBuilder::color(
	const ExternalImage &image,
	AttachmentOps ops
) {
	const auto *resource = std::addressof(image);
	const auto initial = image.image();
	return attachment(
		initial.id,
		image.name(),
		initial,
		[resource] { return resource->image(); },
		image.initial_layout(),
		true,
		ops,
		false
	);
}

FrameGraph::RenderPassBuilder &FrameGraph::RenderPassBuilder::depth(
	const ExternalImage &image,
	AttachmentOps ops
) {
	const auto *resource = std::addressof(image);
	const auto initial = image.image();
	return attachment(
		initial.id,
		image.name(),
		initial,
		[resource] { return resource->image(); },
		image.initial_layout(),
		true,
		ops,
		true
	);
}

FrameGraph::RenderPassBuilder &FrameGraph::RenderPassBuilder::clear_color(
	ImageHandle image,
	std::array<float, 4> value,
	VkAttachmentStoreOp store_op
) {
	AttachmentOps ops;
	ops.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
	ops.store_op = store_op;
	for(size_t i = 0; i < value.size(); i++)
		ops.clear_value.color.float32[i] = value[i];
	return color(image, ops);
}

FrameGraph::RenderPassBuilder &FrameGraph::RenderPassBuilder::clear_depth(
	ImageHandle image,
	float value,
	uint32_t stencil,
	VkAttachmentStoreOp store_op
) {
	AttachmentOps ops;
	ops.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
	ops.store_op = store_op;
	ops.clear_value.depthStencil = {value, stencil};
	return depth(image, ops);
}

FrameGraph::RenderPassBuilder &FrameGraph::RenderPassBuilder::load_color(
	ImageHandle image,
	VkAttachmentStoreOp store_op
) {
	return color(
		image,
		AttachmentOps{
			.load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
			.store_op = store_op,
		}
	);
}

FrameGraph::RenderPassBuilder &FrameGraph::RenderPassBuilder::load_depth(
	ImageHandle image,
	VkAttachmentStoreOp store_op
) {
	return depth(
		image,
		AttachmentOps{
			.load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
			.store_op = store_op,
		}
	);
}

FrameGraph &FrameGraph::add_render_pass(
	const std::string &name,
	RenderSetupFn setup,
	ExecuteFn execute
) & {
	m.passes.push_back({
		.name = name,
		.kind = PassKind::Render,
		.execute = std::move(execute),
	});
	auto builder = RenderPassBuilder::create(m.passes.back());
	setup(builder);
	resolve_resources(m.passes.back());
	m.compiled = false;
	return *this;
}

FrameGraph &&FrameGraph::add_render_pass(
	const std::string &name,
	RenderSetupFn setup,
	ExecuteFn execute
) && {
	static_cast<FrameGraph &>(*this).add_render_pass(
		name,
		std::move(setup),
		std::move(execute)
	);
	return std::move(*this);
}

FrameGraph &FrameGraph::add_compute_pass(
	const std::string &name,
	ComputeSetupFn setup,
	ExecuteFn execute
) & {
	m.passes.push_back({
		.name = name,
		.kind = PassKind::Compute,
		.execute = std::move(execute),
	});
	auto builder = ComputePassBuilder::create(m.passes.back());
	setup(builder);
	resolve_resources(m.passes.back());
	m.compiled = false;
	return *this;
}

FrameGraph &&FrameGraph::add_compute_pass(
	const std::string &name,
	ComputeSetupFn setup,
	ExecuteFn execute
) && {
	static_cast<FrameGraph &>(*this).add_compute_pass(
		name,
		std::move(setup),
		std::move(execute)
	);
	return std::move(*this);
}
