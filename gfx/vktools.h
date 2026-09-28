#pragma once

#include <vulkan/vulkan.h>

#include <stdexcept>
#include <string>

inline void check_vk_result(VkResult result, const char *file, int line) {
	if(result == VK_SUCCESS)
		return;
	throw std::runtime_error(
		"VkResult " + std::to_string(int(result)) + " in " + file +
		":" + std::to_string(line)
	);
}

#define VK_CHECK(f) check_vk_result((f), __FILE__, __LINE__)
