#pragma once

#include <stdexcept>
#define VK_CHECK(f)                                                 \
{                                                                   \
	VkResult result = (f);                                          \
	assert(result == VK_SUCCESS);                                   \
	if(result != VK_SUCCESS) {                                      \
		spdlog::info("VkResult is {} in {} @ {}", (int)result, __FILE__, __LINE__);  \
		std::abort();                                               \
	}                                                               \
}

#define VK_CHECK_HANDLE(f) if(f == VK_NULL_HANDLE) { spdlog::error("handle was null at {}:{}", __FILE__, __LINE__); throw std::runtime_error("fuck"); }
