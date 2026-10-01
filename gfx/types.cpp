#include "gfx/detail/types.h"

#include <stdexcept>

namespace gfx::detail {

Format from_vk_format(VkFormat format) {
	switch(format) {
	case VK_FORMAT_UNDEFINED: return Format::Undefined;
	case VK_FORMAT_R8_UNORM: return Format::R8Unorm;
	case VK_FORMAT_R8G8B8A8_UNORM: return Format::R8G8B8A8Unorm;
	case VK_FORMAT_R8G8B8A8_SRGB: return Format::R8G8B8A8Srgb;
	case VK_FORMAT_B8G8R8A8_UNORM: return Format::B8G8R8A8Unorm;
	case VK_FORMAT_B8G8R8A8_SRGB: return Format::B8G8R8A8Srgb;
	case VK_FORMAT_R16G16B16A16_UNORM: return Format::R16G16B16A16Unorm;
	case VK_FORMAT_R16G16B16A16_SFLOAT: return Format::R16G16B16A16Float;
	case VK_FORMAT_R32G32B32A32_SFLOAT: return Format::R32G32B32A32Float;
	case VK_FORMAT_D16_UNORM: return Format::D16Unorm;
	case VK_FORMAT_D32_SFLOAT: return Format::D32Float;
	case VK_FORMAT_D24_UNORM_S8_UINT: return Format::D24UnormS8Uint;
	default: break;
	}
	throw std::invalid_argument("gfx::Format: unsupported Vulkan format");
}

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

VkCullModeFlagBits to_vk_cull_mode(CullMode mode) {
	switch(mode) {
	case CullMode::None: return VK_CULL_MODE_NONE;
	case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
	case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
	case CullMode::FrontAndBack: return VK_CULL_MODE_FRONT_AND_BACK;
	}
	throw std::invalid_argument("gfx::CullMode: unsupported mode");
}

VkCompareOp to_vk_compare_op(CompareOp op) {
	switch(op) {
	case CompareOp::Never: return VK_COMPARE_OP_NEVER;
	case CompareOp::Less: return VK_COMPARE_OP_LESS;
	case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
	case CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
	case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
	case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
	case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
	case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
	}
	throw std::invalid_argument("gfx::CompareOp: unsupported operation");
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
