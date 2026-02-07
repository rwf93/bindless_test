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

#define VK_ALIGN(size, alignment) ( (size + alignment - 1) & ~(alignment - 1) )
#define VK_ALIGN_BOUNDS(size, alignment) ( alignment > 0 ? VK_ALIGN(size, alignment) : size )
static_assert(VK_ALIGN(1024, 0) != 1024);
static_assert(VK_ALIGN_BOUNDS(1024, 0) == 1024);

#define VK_TRACY_MEMORY_OVERLOADS 		\
