#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

#include "create_utils.h"
#include "vkcontext.h"
#include "vktools.h"

template<typename T>
class GPUBuffer {
	struct M {
		VmaAllocation allocation = VK_NULL_HANDLE;
		VkBuffer buffer = VK_NULL_HANDLE;
		size_t count = 0;
		uint32_t handle = UINT32_MAX;
	} m;

	explicit GPUBuffer(M m) : m(std::move(m)) {}

	void destroy() {
		if(m.buffer != VK_NULL_HANDLE)
			vmaDestroyBuffer(vkctx.allocator, m.buffer, m.allocation);
		m = M{};
	}

public:
	~GPUBuffer() {
		destroy();
	}

	GPUBuffer(const GPUBuffer &) = delete;
	GPUBuffer &operator=(const GPUBuffer &) = delete;

	GPUBuffer(GPUBuffer &&other) noexcept : m(std::move(other.m)) {
		other.m = M{};
	}

	GPUBuffer &operator=(GPUBuffer &&other) noexcept {
		if(this != &other) {
			destroy();
			m = std::move(other.m);
			other.m = M{};
		}
		return *this;
	}

	static GPUBuffer create(std::vector<T> &data) {
		return create(data.data(), data.size());
	}

	static GPUBuffer create(const T *data, size_t count) {
		if(data == nullptr || count == 0)
			throw std::invalid_argument("GPUBuffer::create: data must not be empty");

		const VkDeviceSize size = VkDeviceSize(sizeof(T)) * count;
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
			vkctx.allocator,
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
			vkctx.allocator,
			&staging_buffer_info,
			&staging_allocation_info,
			&staging_buffer,
			&staging_allocation,
			nullptr
		));

		void *mapped = nullptr;
		VK_CHECK(vmaMapMemory(vkctx.allocator, staging_allocation, &mapped));
		std::memcpy(mapped, data, size_t(size));
		vmaUnmapMemory(vkctx.allocator, staging_allocation);

		submit_command([&](VkCommandBuffer command) {
			VkBufferCopy copy_region = {};
			copy_region.size = size;
			device().cmdCopyBuffer(
				command,
				staging_buffer,
				buffer,
				1,
				&copy_region
			);
		});

		vmaDestroyBuffer(vkctx.allocator, staging_buffer, staging_allocation);

		return GPUBuffer(M{
			.allocation = allocation,
			.buffer = buffer,
			.count = count,
			.handle = generate_handle<T>(uint32_t(count), buffer),
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
				vmaUnmapMemory(vkctx.allocator, m.allocation);
			vmaDestroyBuffer(vkctx.allocator, m.buffer, m.allocation);
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

	static SharedBuffer create(size_t count = 1) {
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
		allocation_info.preferredFlags =
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

		VmaAllocation allocation = VK_NULL_HANDLE;
		VkBuffer buffer = VK_NULL_HANDLE;
		VK_CHECK(vmaCreateBuffer(
			vkctx.allocator,
			&buffer_info,
			&allocation_info,
			&buffer,
			&allocation,
			nullptr
		));

		void *mapped = nullptr;
		VK_CHECK(vmaMapMemory(vkctx.allocator, allocation, &mapped));
		std::memset(mapped, 0, size_t(size));

		return SharedBuffer(M{
			.allocation = allocation,
			.buffer = buffer,
			.count = count,
			.handle = generate_handle<T>(uint32_t(count), buffer),
			.mapped = static_cast<T *>(mapped),
		});
	}

	uint32_t handle() const { return m.handle; }
	size_t count() const { return m.count; }
	VkBuffer buffer() const { return m.buffer; }
	VkDeviceSize size_bytes() const { return VkDeviceSize(sizeof(T)) * m.count; }

	T *operator->() const { return m.mapped; }
	T &operator[](size_t index) const { return m.mapped[index]; }
};

// A per-frame set of persistently mapped buffers for frequently updated data.
template<typename T>
class MultiBuffer {
	struct M {
		std::vector<SharedBuffer<T>> buffers;
	} m;

	explicit MultiBuffer(M m) : m(std::move(m)) {}

public:
	MultiBuffer(MultiBuffer &&) noexcept = default;
	MultiBuffer &operator=(MultiBuffer &&) noexcept = default;
	MultiBuffer(const MultiBuffer &) = delete;
	MultiBuffer &operator=(const MultiBuffer &) = delete;

	static MultiBuffer create(size_t count = 1) {
		std::vector<SharedBuffer<T>> buffers;
		buffers.reserve(vkctx.max_frames);
		for(uint32_t i = 0; i < vkctx.max_frames; i++) {
			buffers.emplace_back(with_result_of([&] {
				return SharedBuffer<T>::create(count);
			}));
		}
		return MultiBuffer(M{.buffers = std::move(buffers)});
	}

	SharedBuffer<T> &current() { return m.buffers[frame_index]; }
	const SharedBuffer<T> &current() const { return m.buffers[frame_index]; }

	uint32_t handle() const { return current().handle(); }
	size_t count() const { return current().count(); }
	VkBuffer buffer() const { return current().buffer(); }
	VkDeviceSize size_bytes() const { return current().size_bytes(); }

	T *operator->() const { return current().operator->(); }
	T &operator[](size_t index) const { return current()[index]; }
};
