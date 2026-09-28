#pragma once

#include <VkBootstrap.h>

#include <cstdint>
#include <utility>
#include <vector>

#include "gfx/command_list.h"
#include "gfx/image.h"

namespace gfx {

class Device;

struct PresentationDesc {
	uint32_t width = 0;
	uint32_t height = 0;
	VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
	VkFormat preferred_format = VK_FORMAT_B8G8R8A8_UNORM;
	VkFormat fallback_format = VK_FORMAT_R8G8B8A8_UNORM;
};

class Presentation {
	struct Frame {
		VkFence fence = VK_NULL_HANDLE;
		VkSemaphore submit = VK_NULL_HANDLE;
		VkSemaphore acquire = VK_NULL_HANDLE;
		VkCommandBuffer command = VK_NULL_HANDLE;
	};

	struct M {
		Device *device = nullptr;
		vkb::Swapchain swapchain;
		std::vector<VkImage> images;
		std::vector<VkImageView> views;
		std::vector<ImageId> image_ids;
		VkCommandPool command_pool = VK_NULL_HANDLE;
		std::vector<Frame> frames;
		uint32_t frame_index = 0;
		uint32_t image_index = 0;
		bool recording = false;
	} m;

	explicit Presentation(M m) : m(std::move(m)) {}

public:
	~Presentation();

	Presentation(const Presentation &) = delete;
	Presentation &operator=(const Presentation &) = delete;
	Presentation(Presentation &&) = delete;
	Presentation &operator=(Presentation &&) = delete;

	static Presentation create(
		Device &device,
		VkSurfaceKHR surface,
		const PresentationDesc &desc = {}
	);

	CommandList begin_frame();
	void end_frame();

	VkFormat format() const { return m.swapchain.image_format; }
	VkExtent2D extent() const { return m.swapchain.extent; }
	uint32_t image_count() const { return uint32_t(m.images.size()); }
	ImageRef image() const;
};

} // namespace gfx
