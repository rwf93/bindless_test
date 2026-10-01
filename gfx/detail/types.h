#pragma once

#include <vulkan/vulkan.h>

#include "gfx/types.h"

namespace gfx::detail {

Format from_vk_format(VkFormat format);
VkFormat to_vk_format(Format format);
VkCullModeFlagBits to_vk_cull_mode(CullMode mode);
VkCompareOp to_vk_compare_op(CompareOp op);
VkImageUsageFlags to_vk_image_usage(TextureUsage usage);

} // namespace gfx::detail
