#include "gfx/sdl/surface.h"

#include <SDL3/SDL_vulkan.h>

#include <stdexcept>
#include <string>

namespace gfx::sdl {

Surface Surface::create(Instance &instance, SDL_Window &window) {
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	if(!SDL_Vulkan_CreateSurface(
		&window,
		instance.native(),
		nullptr,
		&surface
	)) {
		throw std::runtime_error(
			std::string("gfx::sdl::Surface::create: ") + SDL_GetError()
		);
	}
	return Surface(M{.instance = &instance, .surface = surface});
}

Surface::~Surface() {
	if(m.surface != VK_NULL_HANDLE)
		vkb::destroy_surface(m.instance->bootstrap(), m.surface);
}

} // namespace gfx::sdl
