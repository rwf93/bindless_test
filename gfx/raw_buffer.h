#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstddef>
#include <span>
#include <utility>

namespace gfx {

class Device;
class UploadBatch;

struct RawBufferDesc {
	VkDeviceSize size = 0;
	VkBufferUsageFlags usage = 0;
	bool host_visible = false;
	bool host_coherent = false;
	bool device_address = false;
};

// Owns one allocation. Upload scheduling and GPU completion belong to UploadBatch.
class RawBuffer {
	struct M {
		Device *device = nullptr;
		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		VkDeviceAddress address = 0;
		VkDeviceSize size = 0;
		VkBufferUsageFlags usage = 0;
		void *mapped = nullptr;
	} m;

	explicit RawBuffer(M m) : m(std::move(m)) {}
	void destroy() noexcept;
	friend class UploadBatch;

public:
	~RawBuffer();
	RawBuffer(const RawBuffer &) = delete;
	RawBuffer &operator=(const RawBuffer &) = delete;
	RawBuffer(RawBuffer &&other) noexcept;
	RawBuffer &operator=(RawBuffer &&other) noexcept;

	static RawBuffer create(Device &device, const RawBufferDesc &desc);
	void write(std::span<const std::byte> bytes, VkDeviceSize offset = 0);
	void invalidate() const;
	VkBuffer native() const { return m.buffer; }
	VkDeviceAddress address() const { return m.address; }
	VkDeviceSize size() const { return m.size; }
	void *data() { return m.mapped; }
	const void *data() const { return m.mapped; }
};

} // namespace gfx
