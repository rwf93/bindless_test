#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <span>
#include <utility>
#include <vector>

#include "gfx/create_utils.h"
#include "gfx/command_list.h"
#include "gfx/device.h"
#include "gfx/vkinfo.h"
#include "gfx/vktools.h"

namespace gfx {

template<typename T>
class Buffer {
	struct M {
		Device *device = nullptr;
		VmaAllocation allocation = VK_NULL_HANDLE;
		VkBuffer buffer = VK_NULL_HANDLE;
		size_t count = 0;
		uint32_t handle = UINT32_MAX;
	} m;

	explicit Buffer(M m) : m(std::move(m)) {}

	void destroy() {
		if(m.buffer != VK_NULL_HANDLE)
			vmaDestroyBuffer(m.device->allocator(), m.buffer, m.allocation);
		m = M{};
	}

public:
	~Buffer() {
		destroy();
	}

	Buffer(const Buffer &) = delete;
	Buffer &operator=(const Buffer &) = delete;

	Buffer(Buffer &&other) noexcept : m(std::move(other.m)) {
		other.m = M{};
	}

	Buffer &operator=(Buffer &&other) noexcept {
		if(this != &other) {
			destroy();
			m = std::move(other.m);
			other.m = M{};
		}
		return *this;
	}

	static Buffer create(Device &device, std::span<const T> data) {
		if(data.empty())
			throw std::invalid_argument("Buffer::create: data must not be empty");

		const VkDeviceSize size = VkDeviceSize(data.size_bytes());
		VkBufferCreateInfo buffer_info = {};
		buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_info.size = size;
		buffer_info.usage =
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo allocation_info = {};
		allocation_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

		VmaAllocation allocation = VK_NULL_HANDLE;
		VkBuffer buffer = VK_NULL_HANDLE;
		VK_CHECK(vmaCreateBuffer(
			device.allocator(),
			&buffer_info,
			&allocation_info,
			&buffer,
			&allocation,
			nullptr
		));

		const auto staging_allocation_info = info::allocation_create_info();
		const auto staging_buffer_info = info::buffer_create_info(
			size,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT
		);
		VkBuffer staging_buffer = VK_NULL_HANDLE;
		VmaAllocation staging_allocation = VK_NULL_HANDLE;
		VK_CHECK(vmaCreateBuffer(
			device.allocator(),
			&staging_buffer_info,
			&staging_allocation_info,
			&staging_buffer,
			&staging_allocation,
			nullptr
		));

		void *mapped = nullptr;
		VK_CHECK(vmaMapMemory(device.allocator(), staging_allocation, &mapped));
		std::memcpy(mapped, data.data(), size_t(size));
		vmaUnmapMemory(device.allocator(), staging_allocation);

		device.submit_and_wait([&](CommandList &commands) {
			VkBufferCopy copy_region = {};
			copy_region.size = size;
			device.dispatch().cmdCopyBuffer(
				commands.native(),
				staging_buffer,
				buffer,
				1,
				&copy_region
			);
		});

		vmaDestroyBuffer(device.allocator(), staging_buffer, staging_allocation);

		return Buffer(M{
			.device = &device,
			.allocation = allocation,
			.buffer = buffer,
			.count = data.size(),
			.handle = device.register_storage_buffer(buffer, size),
		});
	}

	uint32_t handle() const { return m.handle; }
	size_t count() const { return m.count; }
	VkBuffer buffer() const { return m.buffer; }
	VkDeviceSize size_bytes() const { return VkDeviceSize(sizeof(T)) * m.count; }
};

template<typename T>
class SharedBuffer {
	struct M {
		Device *device = nullptr;
		VmaAllocation allocation = VK_NULL_HANDLE;
		VkBuffer buffer = VK_NULL_HANDLE;
		size_t count = 0;
		uint32_t handle = UINT32_MAX;
		T *mapped = nullptr;
	} m;

	explicit SharedBuffer(M m) : m(std::move(m)) {}

	void destroy() {
		if(m.allocation != VK_NULL_HANDLE) {
			if(m.mapped)
				vmaUnmapMemory(m.device->allocator(), m.allocation);
			vmaDestroyBuffer(m.device->allocator(), m.buffer, m.allocation);
		}
		m = M{};
	}

public:
	~SharedBuffer() {
		destroy();
	}

	SharedBuffer(const SharedBuffer &) = delete;
	SharedBuffer &operator=(const SharedBuffer &) = delete;

	SharedBuffer(SharedBuffer &&other) noexcept : m(std::move(other.m)) {
		other.m = M{};
	}

	SharedBuffer &operator=(SharedBuffer &&other) noexcept {
		if(this != &other) {
			destroy();
			m = std::move(other.m);
			other.m = M{};
		}
		return *this;
	}

	static SharedBuffer create(Device &device, size_t count = 1) {
		if(count == 0)
			throw std::invalid_argument("SharedBuffer::create: count must be greater than zero");

		const VkDeviceSize size = VkDeviceSize(sizeof(T)) * count;
		const auto buffer_info = info::buffer_create_info(
			size,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
		);

		VmaAllocationCreateInfo allocation_info = {};
		allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
		allocation_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
		allocation_info.requiredFlags =
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

		VmaAllocation allocation = VK_NULL_HANDLE;
		VkBuffer buffer = VK_NULL_HANDLE;
		VK_CHECK(vmaCreateBuffer(
			device.allocator(),
			&buffer_info,
			&allocation_info,
			&buffer,
			&allocation,
			nullptr
		));

		void *mapped = nullptr;
		VK_CHECK(vmaMapMemory(device.allocator(), allocation, &mapped));
		std::memset(mapped, 0, size_t(size));

		return SharedBuffer(M{
			.device = &device,
			.allocation = allocation,
			.buffer = buffer,
			.count = count,
			.handle = device.register_storage_buffer(buffer, size),
			.mapped = static_cast<T *>(mapped),
		});
	}

	uint32_t handle() const { return m.handle; }
	size_t count() const { return m.count; }
	VkBuffer buffer() const { return m.buffer; }
	VkDeviceSize size_bytes() const { return VkDeviceSize(sizeof(T)) * m.count; }

	T *data() { return m.mapped; }
	const T *data() const { return m.mapped; }
	T *operator->() { return data(); }
	const T *operator->() const { return data(); }
	T &operator[](size_t index) { return data()[index]; }
	const T &operator[](size_t index) const { return data()[index]; }
	void invalidate() const {
		VK_CHECK(vmaInvalidateAllocation(
			m.device->allocator(),
			m.allocation,
			0,
			VK_WHOLE_SIZE
		));
	}
};

// A per-frame set of persistently mapped buffers for frequently updated data.
template<typename T>
class MultiBuffer {
	struct M {
		Device *device = nullptr;
		std::vector<SharedBuffer<T>> buffers;
	} m;

	explicit MultiBuffer(M m) : m(std::move(m)) {}

public:
	MultiBuffer(MultiBuffer &&) noexcept = default;
	MultiBuffer &operator=(MultiBuffer &&) noexcept = default;
	MultiBuffer(const MultiBuffer &) = delete;
	MultiBuffer &operator=(const MultiBuffer &) = delete;

	static MultiBuffer create(Device &device, size_t count = 1) {
		std::vector<SharedBuffer<T>> buffers;
		buffers.reserve(device.frames_in_flight());
		for(uint32_t i = 0; i < device.frames_in_flight(); i++) {
			buffers.emplace_back(with_result_of([&] {
				return SharedBuffer<T>::create(device, count);
			}));
		}
		return MultiBuffer(M{
			.device = &device,
			.buffers = std::move(buffers),
		});
	}

	SharedBuffer<T> &current() { return m.buffers[m.device->frame_index()]; }
	const SharedBuffer<T> &current() const { return m.buffers[m.device->frame_index()]; }

	uint32_t handle() const { return current().handle(); }
	size_t count() const { return current().count(); }
	VkBuffer buffer() const { return current().buffer(); }
	VkDeviceSize size_bytes() const { return current().size_bytes(); }

	T *data() { return current().data(); }
	const T *data() const { return current().data(); }
	T *operator->() { return data(); }
	const T *operator->() const { return data(); }
	T &operator[](size_t index) { return current()[index]; }
	const T &operator[](size_t index) const { return current()[index]; }
};

} // namespace gfx
