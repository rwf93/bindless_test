#include "framegraph.h"

#include <stdexcept>

namespace {

ImageRef describe_image(
	ImageRef image,
	ImageId id,
	VkFormat format,
	VkExtent3D extent,
	uint32_t layer_count
) {
	image.id = id;
	image.desc.format = format;
	image.desc.extent = extent;
	image.desc.layer_count = layer_count;

	if(image.subresources.aspectMask == 0)
		image.subresources.aspectMask = image_aspect_mask(format);
	if(image.subresources.levelCount == 0)
		image.subresources.levelCount = image.desc.mip_count;
	if(image.subresources.layerCount == 0)
		image.subresources.layerCount = layer_count;
	return image;
}

} // namespace

ImageRef FrameGraph::ExternalImage::image() const {
	return describe_image(
		m.resolve(),
		m.id,
		m.desc.format,
		m.desc.extent,
		m.desc.layer_count
	);
}

FrameGraph::ExternalImage FrameGraph::ExternalImage::create(
	std::string name,
	ImageDesc desc,
	std::function<ImageRef()> resolve
) {
	return ExternalImage(M{
		.id = allocate_image_id(),
		.resource_name = std::move(name),
		.desc = desc,
		.resolve = std::move(resolve),
	});
}

void FrameGraph::validate(ImageHandle handle) const {
	if(!handle.valid() || handle.index >= m.images.size())
		throw std::runtime_error("FrameGraph: invalid image handle");
}

void FrameGraph::validate(BufferHandle handle) const {
	if(!handle.valid() || handle.index >= m.buffers.size())
		throw std::runtime_error("FrameGraph: invalid buffer handle");
}

FrameGraph::ImageHandle FrameGraph::resolve_image(
	ImageReference reference,
	bool attachment
) {
	if(reference.identity == 0 || !reference.initial.valid()) {
		throw std::runtime_error(
			"FrameGraph: resource wrapper returned an invalid image reference"
		);
	}

	if(auto it = m.image_by_identity.find(reference.identity);
		it != m.image_by_identity.end())
	{
		auto &resource = m.images[it->second];
		resource.reset_each_frame |= reference.reset_each_frame;

		// Some resources expose a different view for layered rendering. Prefer
		// the attachment resolver and its view metadata when one is supplied.
		if(attachment) {
			resource.image = reference.initial;
			resource.desc.format = reference.initial.desc.format;
			resource.desc.extent = reference.initial.desc.extent;
			resource.desc.layer_count = reference.initial.desc.layer_count;
			resource.resolve = std::move(reference.resolve);
		}
		return {it->second};
	}

	ImageDesc desc{
		.format = reference.initial.desc.format,
		.extent = reference.initial.desc.extent,
		.layer_count = reference.initial.desc.layer_count,
		.initial_layout = reference.initial_layout,
	};
	if(desc.format == VK_FORMAT_UNDEFINED ||
		desc.extent.width == 0 ||
		desc.extent.height == 0 ||
		desc.extent.depth == 0 ||
		desc.layer_count == 0)
	{
		throw std::runtime_error(
			"FrameGraph: resource wrapper is missing image format/extent metadata"
		);
	}

	const uint32_t index = uint32_t(m.images.size());
	if(reference.name.empty())
		reference.name = "image_" + std::to_string(index);
	else if(m.image_by_name.contains(reference.name))
		reference.name += "_" + std::to_string(index);

	ImageState initial{
		.layout = desc.initial_layout,
		.stage = desc.initial_stage,
		.access = desc.initial_access,
	};
	if(initial.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
		initial.stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
		initial.access = VK_ACCESS_2_NONE;
	}

	m.images.push_back({
		.name = reference.name,
		.image = reference.initial,
		.desc = desc,
		.initial_state = initial,
		.state = initial,
		.resolve = std::move(reference.resolve),
		.reset_each_frame = reference.reset_each_frame,
	});
	m.image_by_name.emplace(reference.name, index);
	m.image_by_identity.emplace(reference.identity, index);
	return {index};
}

FrameGraph::BufferHandle FrameGraph::resolve_buffer(
	BufferReference reference
) {
	if(auto it = m.buffer_by_identity.find(reference.identity);
		it != m.buffer_by_identity.end())
	{
		auto &resource = m.buffers[it->second];
		resource.reset_each_frame |= reference.reset_each_frame;
		return {it->second};
	}

	if(reference.initial == VK_NULL_HANDLE) {
		throw std::runtime_error(
			"FrameGraph: resource wrapper returned a null buffer"
		);
	}
	if(reference.size == 0) {
		throw std::runtime_error(
			"FrameGraph: resource wrapper returned a zero-sized buffer"
		);
	}

	const uint32_t index = uint32_t(m.buffers.size());
	const std::string name = "buffer_" + std::to_string(index);
	BufferDesc desc{.size = reference.size};
	BufferState initial{desc.initial_stage, desc.initial_access};
	m.buffers.push_back({
		.name = name,
		.buffer = reference.initial,
		.desc = desc,
		.initial_state = initial,
		.state = initial,
		.resolve = std::move(reference.resolve),
		.reset_each_frame = reference.reset_each_frame,
	});
	m.buffer_by_name.emplace(name, index);
	m.buffer_by_identity.emplace(reference.identity, index);
	return {index};
}

void FrameGraph::resolve_resources(Pass &pass) {
	for(auto &use : pass.images) {
		if(use.reference) {
			use.handle = resolve_image(std::move(*use.reference), false);
			use.reference.reset();
		} else {
			validate(use.handle);
		}
	}

	for(auto &use : pass.buffers) {
		if(use.reference) {
			use.handle = resolve_buffer(std::move(*use.reference));
			use.reference.reset();
		} else {
			validate(use.handle);
		}
	}

	for(auto &attachment : pass.attachments) {
		if(attachment.reference) {
			attachment.handle = resolve_image(
				std::move(*attachment.reference),
				true
			);
			attachment.reference.reset();
		} else {
			validate(attachment.handle);
		}
	}
}

void FrameGraph::refresh_external_resources() {
	for(auto &resource : m.images) {
		if(!resource.resolve)
			continue;

		auto image = resource.resolve();
		if(image.image == VK_NULL_HANDLE || image.view == VK_NULL_HANDLE) {
			throw std::runtime_error(
				"FrameGraph: image provider for '" + resource.name +
				"' returned null handles"
			);
		}

		const bool changed =
			image.image != resource.image.image ||
			image.view != resource.image.view;
		resource.image = describe_image(
			image,
			resource.image.id,
			resource.desc.format,
			resource.desc.extent,
			resource.desc.layer_count
		);
		if(changed || resource.reset_each_frame)
			resource.state = resource.initial_state;
	}

	for(auto &resource : m.buffers) {
		if(!resource.resolve)
			continue;

		const auto buffer = resource.resolve();
		if(buffer == VK_NULL_HANDLE) {
			throw std::runtime_error(
				"FrameGraph: buffer provider for '" + resource.name +
				"' returned a null handle"
			);
		}

		const bool changed = buffer != resource.buffer;
		resource.buffer = buffer;
		if(changed || resource.reset_each_frame)
			resource.state = resource.initial_state;
	}
}
