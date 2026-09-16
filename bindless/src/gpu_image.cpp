#include "gpu_image.h"

#include <atomic>
#include <stdexcept>

#include "vkcontext.h"
#include "vktools.h"

ImageId allocate_image_id() {
	static std::atomic<ImageId> next_id = 1;
	return next_id.fetch_add(1, std::memory_order_relaxed);
}

GPUImage GPUImage::create(const ImageDesc &desc) {
	if(desc.format == VK_FORMAT_UNDEFINED ||
		desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0 ||
		desc.mip_count == 0 || desc.layer_count == 0)
		throw std::runtime_error("GPUImage::create: invalid image description");

	VkImageCreateInfo image_info = {};
	image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_info.flags = desc.flags;
	image_info.imageType = desc.image_type;
	image_info.format = desc.format;
	image_info.extent = desc.extent;
	image_info.mipLevels = desc.mip_count;
	image_info.arrayLayers = desc.layer_count;
	image_info.samples = desc.samples;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage = desc.usage;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo allocation_info = {};
	allocation_info.usage = VMA_MEMORY_USAGE_AUTO;

	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	VK_CHECK(vmaCreateImage(
		vkctx.allocator,
		&image_info,
		&allocation_info,
		&image,
		&allocation,
		nullptr
	));

	VkImageSubresourceRange subresources = {};
	subresources.aspectMask = image_aspect_mask(desc.format);
	subresources.baseMipLevel = 0;
	subresources.levelCount = desc.mip_count;
	subresources.baseArrayLayer = 0;
	subresources.layerCount = desc.layer_count;

	VkImageViewCreateInfo view_info = {};
	view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_info.image = image;
	view_info.viewType = desc.view_type;
	view_info.format = desc.format;
	view_info.subresourceRange = subresources;

	VkImageView view = VK_NULL_HANDLE;
	VK_CHECK(device().createImageView(&view_info, nullptr, &view));

	std::vector<VkImageView> owned_views;
	owned_views.push_back(view);

	return GPUImage(M{
		.primary = ImageRef{
			.id = allocate_image_id(),
			.image = image,
			.view = view,
			.desc = desc,
			.subresources = subresources,
		},
		.allocation = allocation,
		.owned_views = std::move(owned_views),
	});
}

GPUImage::~GPUImage() {
	destroy();
}

GPUImage::GPUImage(GPUImage &&other) noexcept
	: m(std::move(other.m))
{
	other.m.primary = {};
	other.m.allocation = VK_NULL_HANDLE;
	other.m.owned_views.clear();
}

GPUImage &GPUImage::operator=(GPUImage &&other) noexcept {
	if(this == &other)
		return *this;

	destroy();
	m = std::move(other.m);
	other.m.primary = {};
	other.m.allocation = VK_NULL_HANDLE;
	other.m.owned_views.clear();
	return *this;
}

void GPUImage::destroy() {
	if(m.primary.image == VK_NULL_HANDLE)
		return;

	for(auto view : m.owned_views)
		if(view != VK_NULL_HANDLE)
			device().destroyImageView(view, nullptr);

	if(m.allocation != VK_NULL_HANDLE)
		vmaDestroyImage(vkctx.allocator, m.primary.image, m.allocation);

	m = M{};
}

ImageRef GPUImage::ref() const {
	return m.primary;
}

ImageRef GPUImage::create_view(
	VkImageViewType view_type,
	VkImageSubresourceRange subresources,
	uint32_t exposed_layer_count
) {
	if(m.primary.image == VK_NULL_HANDLE)
		throw std::runtime_error("GPUImage::create_view: image is empty");
	if(exposed_layer_count == 0)
		throw std::runtime_error("GPUImage::create_view: exposed_layer_count must be greater than zero");

	VkImageViewCreateInfo view_info = {};
	view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_info.image = m.primary.image;
	view_info.viewType = view_type;
	view_info.format = m.primary.desc.format;
	view_info.subresourceRange = subresources;

	VkImageView view = VK_NULL_HANDLE;
	VK_CHECK(device().createImageView(&view_info, nullptr, &view));
	m.owned_views.push_back(view);

	auto desc = m.primary.desc;
	desc.view_type = view_type;
	desc.layer_count = exposed_layer_count;
	return ImageRef{
		.id = m.primary.id,
		.image = m.primary.image,
		.view = view,
		.desc = desc,
		.subresources = subresources,
	};
}
