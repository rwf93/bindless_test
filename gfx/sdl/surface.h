#pragma once

#include <vulkan/vulkan.h>

#include <utility>

#include "gfx/instance.h"

struct SDL_Window;

namespace gfx::sdl {

class Surface {
	struct M {
		Instance *instance = nullptr;
		VkSurfaceKHR surface = VK_NULL_HANDLE;
	} m;

	explicit Surface(M m) : m(std::move(m)) {}

public:
	~Surface();

	Surface(const Surface &) = delete;
	Surface &operator=(const Surface &) = delete;
	Surface(Surface &&) = delete;
	Surface &operator=(Surface &&) = delete;

	static Surface create(Instance &instance, SDL_Window &window);

	VkSurfaceKHR native() const { return m.surface; }
};

} // namespace gfx::sdl
