#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace gfx {

class Device;

using ImageId = uint64_t;

namespace detail {
ImageId allocate_image_id();
}

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
// and VkImageView lifetime belong exclusively to Image.
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

class Image {
	struct M {
		gfx::Device *device = nullptr;
		ImageRef primary;
		VmaAllocation allocation = VK_NULL_HANDLE;
		std::vector<VkImageView> owned_views;
	} m;

	explicit Image(M m) : m(std::move(m)) {}
	void destroy();

public:
	~Image();

	Image(const Image &) = delete;
	Image &operator=(const Image &) = delete;
	Image(Image &&other) noexcept;
	Image &operator=(Image &&other) noexcept;

	static Image create(Device &device, const ImageDesc &desc);

	ImageRef ref() const;
	ImageRef create_view(
		VkImageViewType view_type,
		VkImageSubresourceRange subresources,
		uint32_t exposed_layer_count
	);
};

} // namespace gfx
