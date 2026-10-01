#pragma once

#include <vulkan/vulkan.h>

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
#include "gfx/raw_buffer.h"
#include "gfx/upload_batch.h"

namespace gfx {

template<typename T>
class Buffer {
	struct M {
		RawBuffer storage;
		size_t count = 0;
		uint32_t handle = UINT32_MAX;
	} m;

	explicit Buffer(M m) : m(std::move(m)) {}

public:
	~Buffer() = default;
	Buffer(const Buffer &) = delete;
	Buffer &operator=(const Buffer &) = delete;
	Buffer(Buffer &&) noexcept = default;
	Buffer &operator=(Buffer &&) noexcept = default;

	static Buffer create(Device &device, std::span<const T> data) {
		auto upload = UploadBatch::create(device);
		auto result = create(upload, data);
		upload.submit().wait();
		return result;
	}

	static Buffer create(UploadBatch &upload, std::span<const T> data) {
		if(data.empty())
			throw std::invalid_argument("Buffer::create: data must not be empty");

		Device &device = upload.device();
		const VkDeviceSize size = VkDeviceSize(data.size_bytes());
		auto storage = RawBuffer::create(device, RawBufferDesc{
			.size = size,
			.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		});
		upload.upload(storage, std::as_bytes(data));
		const uint32_t handle = device.register_storage_buffer(storage.native(), size);

		return Buffer(M{
			.storage = std::move(storage),
			.count = data.size(),
			.handle = handle,
		});
	}

	uint32_t handle() const { return m.handle; }
	size_t count() const { return m.count; }
	VkBuffer buffer() const { return m.storage.native(); }
	VkDeviceSize size_bytes() const { return VkDeviceSize(sizeof(T)) * m.count; }
};

template<typename T>
class SharedBuffer {
	struct M {
		RawBuffer storage;
		size_t count = 0;
		uint32_t handle = UINT32_MAX;
	} m;

	explicit SharedBuffer(M m) : m(std::move(m)) {}

public:
	~SharedBuffer() = default;
	SharedBuffer(const SharedBuffer &) = delete;
	SharedBuffer &operator=(const SharedBuffer &) = delete;
	SharedBuffer(SharedBuffer &&) noexcept = default;
	SharedBuffer &operator=(SharedBuffer &&) noexcept = default;

	static SharedBuffer create(Device &device, size_t count = 1) {
		if(count == 0)
			throw std::invalid_argument("SharedBuffer::create: count must be greater than zero");

		const VkDeviceSize size = VkDeviceSize(sizeof(T)) * count;
		auto storage = RawBuffer::create(device, RawBufferDesc{
			.size = size,
			.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			.host_visible = true,
			.host_coherent = true,
		});
		std::memset(storage.data(), 0, size_t(size));
		const uint32_t handle = device.register_storage_buffer(storage.native(), size);

		return SharedBuffer(M{
			.storage = std::move(storage),
			.count = count,
			.handle = handle,
		});
	}

	uint32_t handle() const { return m.handle; }
	size_t count() const { return m.count; }
	VkBuffer buffer() const { return m.storage.native(); }
	VkDeviceSize size_bytes() const { return VkDeviceSize(sizeof(T)) * m.count; }

	T *data() { return static_cast<T *>(m.storage.data()); }
	const T *data() const { return static_cast<const T *>(m.storage.data()); }
	T *operator->() { return data(); }
	const T *operator->() const { return data(); }
	T &operator[](size_t index) { return data()[index]; }
	const T &operator[](size_t index) const { return data()[index]; }
	void invalidate() const { m.storage.invalidate(); }
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
