#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <utility>
#include <vector>

using ImageId = uint64_t;

ImageId allocate_image_id();

struct ImageDesc {
	VkImageType image_type = VK_IMAGE_TYPE_2D;
	VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_2D;
	VkFormat format = VK_FORMAT_UNDEFINED;
	VkExtent3D extent = {0, 0, 1};
	VkImageUsageFlags usage = 0;
	VkImageCreateFlags flags = 0;
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
	uint32_t mip_count = 1;
	uint32_t layer_count = 1;
};

// A copyable, non-owning reference to one view of a GPU image. The allocation
// and VkImageView lifetime belong exclusively to GPUImage.
struct ImageRef {
	ImageId id = 0;
	VkImage image = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	ImageDesc desc{};
	VkImageSubresourceRange subresources{};

	bool valid() const {
		return id != 0 && image != VK_NULL_HANDLE && view != VK_NULL_HANDLE;
	}
};

class GPUImage {
	struct M {
		ImageRef primary;
		VmaAllocation allocation = VK_NULL_HANDLE;
		std::vector<VkImageView> owned_views;
	} m;

	explicit GPUImage(M m) : m(std::move(m)) {}
	void destroy();

public:
	~GPUImage();

	GPUImage(const GPUImage &) = delete;
	GPUImage &operator=(const GPUImage &) = delete;
	GPUImage(GPUImage &&other) noexcept;
	GPUImage &operator=(GPUImage &&other) noexcept;

	static GPUImage create(const ImageDesc &desc);

	ImageRef ref() const;
	ImageRef create_view(
		VkImageViewType view_type,
		VkImageSubresourceRange subresources,
		uint32_t exposed_layer_count
	);
};
