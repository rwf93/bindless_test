#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "gpu_image.h"

enum class TextureColorSpace : uint8_t {
	Linear,
	SRGB,
};

namespace texture_detail {

VkImageLayout initial_layout_for(VkFormat format);

GPUImage create_image(
	ImageDesc desc,
	VkImageLayout initial_layout
);

void upload_image(
	const ImageRef &image,
	VkExtent3D extent,
	VkFormat format,
	std::span<const std::byte> data,
	VkImageLayout final_layout
);

void set_debug_name(
	VkObjectType type,
	uint64_t handle,
	std::string_view name
);

void set_image_debug_names(
	const ImageRef &image,
	std::string_view name
);

} // namespace texture_detail
