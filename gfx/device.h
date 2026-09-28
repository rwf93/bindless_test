#pragma once

#include <VkBootstrap.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "gfx/instance.h"

namespace gfx {

class CommandList;
class Presentation;
class Texture2D;
class Texture2DArray;
class Texture3D;
class TextureCube;
struct ImageRef;
template<typename T> class Buffer;
template<typename T> class SharedBuffer;

inline constexpr uint32_t max_bindless_resources = 2u << 12;

struct DeviceDesc {
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	uint32_t frames_in_flight = 2;
	bool graphics = true;
};

class Device {
	struct M {
		vkb::Device device;
		vkb::DispatchTable dispatch;
		VmaAllocator allocator = VK_NULL_HANDLE;
		VkQueue queue = VK_NULL_HANDLE;
		VkQueue present_queue = VK_NULL_HANDLE;
		uint32_t queue_family = 0;
		VkCommandPool immediate_pool = VK_NULL_HANDLE;
		VkCommandBuffer immediate_command = VK_NULL_HANDLE;
		VkFence immediate_fence = VK_NULL_HANDLE;
		VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
		VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
		VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> descriptor_sets;
		std::vector<VkSampler> samplers;
		uint32_t storage_index = 0;
		uint32_t texture_index = 0;
		uint32_t frame_index = 0;
		uint32_t frames_in_flight = 0;
		bool graphics = true;
	} m;

	explicit Device(M m) : m(std::move(m)) {}

	void update_all_descriptor_sets(const VkWriteDescriptorSet &write);
	void set_frame_index(uint32_t index);
	bool graphics_capable() const { return m.graphics; }
	VkQueue present_queue() const { return m.present_queue; }
	uint32_t register_storage_buffer(VkBuffer buffer, VkDeviceSize size);
	uint32_t register_image(const ImageRef &image);
	void transition(
		VkCommandBuffer command,
		VkImage image,
		VkImageLayout current_layout,
		VkImageLayout new_layout,
		VkImageAspectFlags aspect_mask
	);

	friend class CommandList;
	friend class Presentation;
	friend class Texture2D;
	friend class Texture2DArray;
	friend class Texture3D;
	friend class TextureCube;
	template<typename T> friend class Buffer;
	template<typename T> friend class SharedBuffer;

public:
	~Device();

	Device(const Device &) = delete;
	Device &operator=(const Device &) = delete;
	Device(Device &&) = delete;
	Device &operator=(Device &&) = delete;

	static Device create(Instance &instance, const DeviceDesc &desc = {});

	vkb::Device &bootstrap() { return m.device; }
	const vkb::Device &bootstrap() const { return m.device; }
	vkb::DispatchTable &dispatch() { return m.dispatch; }
	const vkb::DispatchTable &dispatch() const { return m.dispatch; }
	VkDevice native() const { return m.device.device; }
	VkPhysicalDevice physical_device() const {
		return m.device.physical_device.physical_device;
	}
	VmaAllocator allocator() const { return m.allocator; }
	VkQueue queue() const { return m.queue; }
	uint32_t queue_family() const { return m.queue_family; }
	VkDescriptorPool descriptor_pool() const { return m.descriptor_pool; }
	VkPipelineLayout pipeline_layout() const { return m.pipeline_layout; }
	VkDescriptorSet descriptor_set() const { return m.descriptor_sets.at(m.frame_index); }
	uint32_t frame_index() const { return m.frame_index; }
	uint32_t frames_in_flight() const { return m.frames_in_flight; }
	void wait_idle();
	void submit_and_wait(const std::function<void(CommandList &)> &record);
};

VkImageAspectFlags image_aspect_mask(VkFormat format);

} // namespace gfx
