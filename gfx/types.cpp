#include "gfx/detail/types.h"

#include <stdexcept>

namespace gfx::detail {

VkFormat to_vk_format(Format format) {
	switch(format) {
	case Format::Undefined: return VK_FORMAT_UNDEFINED;
	case Format::R8Unorm: return VK_FORMAT_R8_UNORM;
	case Format::R8G8B8A8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
	case Format::R8G8B8A8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
	case Format::B8G8R8A8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
	case Format::B8G8R8A8Srgb: return VK_FORMAT_B8G8R8A8_SRGB;
	case Format::R16G16B16A16Unorm: return VK_FORMAT_R16G16B16A16_UNORM;
	case Format::R16G16B16A16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
	case Format::R32G32B32A32Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
	case Format::D16Unorm: return VK_FORMAT_D16_UNORM;
	case Format::D32Float: return VK_FORMAT_D32_SFLOAT;
	case Format::D24UnormS8Uint: return VK_FORMAT_D24_UNORM_S8_UINT;
	}
	throw std::invalid_argument("gfx::Format: unsupported format");
}

VkImageUsageFlags to_vk_image_usage(TextureUsage usage) {
	VkImageUsageFlags result = 0;
	if(has_usage(usage, TextureUsage::Sampled))
		result |= VK_IMAGE_USAGE_SAMPLED_BIT;
	if(has_usage(usage, TextureUsage::Storage))
		result |= VK_IMAGE_USAGE_STORAGE_BIT;
	if(has_usage(usage, TextureUsage::ColorAttachment))
		result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if(has_usage(usage, TextureUsage::DepthStencilAttachment))
		result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if(has_usage(usage, TextureUsage::TransferSource))
		result |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	if(has_usage(usage, TextureUsage::TransferDestination))
		result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	return result;
}

} // namespace gfx::detail
