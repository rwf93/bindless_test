#pragma once

#include <vulkan/vulkan.h>

#include "gfx/types.h"

namespace gfx::detail {

VkFormat to_vk_format(Format format);
VkImageUsageFlags to_vk_image_usage(TextureUsage usage);

} // namespace gfx::detail
