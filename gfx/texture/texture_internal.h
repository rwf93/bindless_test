#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <span>
#include <string_view>

#include "gfx/image.h"

namespace gfx {
class Device;
namespace texture_detail {

VkImageLayout initial_layout_for(VkFormat format);

Image create_image(
	Device &device,
	ImageDesc desc,
	VkImageLayout initial_layout
);

void upload_image(
	Device &device,
	const ImageRef &image,
	VkExtent3D extent,
	VkFormat format,
	std::span<const std::byte> data,
	VkImageLayout final_layout
);

void set_debug_name(
	Device &device,
	VkObjectType type,
	uint64_t handle,
	std::string_view name
);

void set_image_debug_names(
	Device &device,
	const ImageRef &image,
	std::string_view name
);

} // namespace texture_detail
} // namespace gfx
