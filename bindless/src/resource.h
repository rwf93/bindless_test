#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <filesystem>
#include <functional>
#include <vector>
#include <array>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <string>

#include "vkcontext.h"
#include "vktools.h"

glm::mat4 calculate_model_matrix(glm::vec3 translation, glm::vec3 rotation, glm::vec3 scale);

template<typename Lambda>
class DeferScopeGuard {
	struct M {
		Lambda &&lambda;
	} m;
	explicit DeferScopeGuard(M m) : m(std::move(m)) {}
public:
	static DeferScopeGuard create(Lambda &&lambda) {
		return DeferScopeGuard(M {
			.lambda = lambda
		});
	}
	~DeferScopeGuard() { m.lambda(); };
};

class Texture2D {
	struct M {
		AllocatedImage image;
		uint32_t handle;
		uint32_t width;
		uint32_t height;
	} m;

	explicit Texture2D(M m) : m(std::move(m)) {}

	static void transfer_data(
		uint32_t width,
		uint32_t height,
		AllocatedImage *image,
		void *data,
		VkImageLayout final_layout
	);

	static AllocatedImage create_image(
		uint32_t width,
		uint32_t height,
		VkFormat format,
		VkImageUsageFlags usage
	);
public:
	static Texture2D create(uint32_t width, uint32_t height, VkFormat format, std::function<uint32_t(int x, int y)> &&fn);
	static Texture2D create(uint32_t width, uint32_t height, AllocatedImage image);
	static Texture2D create_empty(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlagBits usage);
	static Texture2D create(uint32_t width, uint32_t height, VkFormat format, void *data);

	// Load a 4-channel RGBA texture from an image file on disk (stb_image).
	// Returns an empty Texture2D (handle = UINT32_MAX) if the file can't be loaded.
	static Texture2D load_file(const std::filesystem::path &path);

	// Path-keyed cache of loaded textures, so multiple materials referencing
	// the same file share one GPU Texture2D. Mirrors Material::material_cache.
	static std::unordered_map<std::string, Texture2D> cache;

	// Load `path` through the cache. Returns the bindless handle of the
	// (possibly already-cached) texture. Failed loads still get a cache
	// entry (with handle = UINT32_MAX) so we don't re-attempt every frame.
	static uint32_t load_cached(const std::filesystem::path &path);

	uint32_t handle();
	AllocatedImage image();
};

template<typename T> class GPUBuffer {
	struct M {
		VmaAllocation allocation;
		VkBuffer buffer;
		size_t count;
		uint32_t handle;
	} m;

	explicit GPUBuffer<T>(M m) : m(std::move(m)) {}
public:
	~GPUBuffer<T>() {
		vmaDestroyBuffer(vkctx.allocator, m.buffer, m.allocation);
	}

	GPUBuffer<T>(const GPUBuffer<T>&) = delete;  // No copying
	GPUBuffer<T>& operator=(const GPUBuffer<T>&) = delete;
	GPUBuffer<T>(GPUBuffer<T>&& other) noexcept : m(std::move(other.m)) {
		memset(&other.m, 0, sizeof(M));
	}
	GPUBuffer<T>& operator=(GPUBuffer<T>&& other) noexcept {
		if (this != &other) {
			vmaDestroyBuffer(vkctx.allocator, m.buffer, m.allocation);
			m = std::move(other.m);
			memset(&other.m, 0, sizeof(M));
		}
		return *this;
	}

	static GPUBuffer<T> create(std::vector<T> &data) {
		return GPUBuffer<T>::create(data.data(), data.size());
	}

	static GPUBuffer<T> create(T *data, size_t count) {
		VkBufferCreateInfo buffer_info = {};
		buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_info.size = sizeof(T) * count;
		buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo alloc_info = {};
		alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

		VmaAllocation allocation;
		VkBuffer buffer;
		VK_CHECK(vmaCreateBuffer(vkctx.allocator, &buffer_info, &alloc_info, &buffer, &allocation, nullptr));

		VmaAllocationCreateInfo staging_alloc_info = info::allocation_create_info();
		VkBuffer staging_buffer;
		VmaAllocation staging_allocation;
		VkBufferCreateInfo staging_info = info::buffer_create_info(sizeof(T) * count, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
		VK_CHECK(vmaCreateBuffer(vkctx.allocator, &staging_info, &staging_alloc_info, &staging_buffer, &staging_allocation, nullptr));

		// Copy data to the buffer
		submit_command([&](VkCommandBuffer command) {
			void* mapped;
			vmaMapMemory(vkctx.allocator, staging_allocation, &mapped);
			memcpy(mapped, data, sizeof(T) * count);
			vmaUnmapMemory(vkctx.allocator, staging_allocation);

			VkBufferCopy copy_region = {};
			copy_region.size = sizeof(T) * count;
			device().cmdCopyBuffer(command, staging_buffer, buffer, 1, &copy_region);
		});

		vmaDestroyBuffer(vkctx.allocator, staging_buffer, staging_allocation);

		VkDescriptorBufferInfo buffer_info_desc = {};
		buffer_info_desc.buffer = buffer;
		buffer_info_desc.offset = 0;
		buffer_info_desc.range = sizeof(T) * count;

		VkWriteDescriptorSet write_desc = {};
		write_desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write_desc.dstBinding = 0;
		write_desc.dstArrayElement = bindless_storage_index;
		write_desc.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		write_desc.descriptorCount = 1;
		write_desc.pBufferInfo = &buffer_info_desc;

		update_descriptor_all_frames(write_desc, bindless_storage_desc);

		return GPUBuffer<T>(M {
			.allocation = allocation,
			.buffer = buffer,
			.count = count,
			.handle = bindless_storage_index++,
		});
	}

	uint32_t handle() {
		return m.handle;
	}

	size_t count() {
		return m.count;
	}
};

template<typename T> class SharedBuffer {
	struct M {
		VmaAllocation allocation = VK_NULL_HANDLE;
		VkBuffer buffer = VK_NULL_HANDLE;
		size_t count = 0;
		uint32_t handle = 0;
		T *wrap = nullptr;
	} m;

	explicit SharedBuffer<T>(M m) : m(std::move(m)) {}
public:
	SharedBuffer<T>() = default;

	~SharedBuffer<T>() {
		if(m.allocation)
			vmaUnmapMemory(vkctx.allocator, m.allocation);
		vmaDestroyBuffer(vkctx.allocator, m.buffer, m.allocation);
	}

	SharedBuffer<T>(const SharedBuffer<T>&) = delete;  // No copying
	SharedBuffer<T>& operator=(const SharedBuffer<T>&) = delete;
	SharedBuffer<T>(SharedBuffer<T>&& other) noexcept : m(std::move(other.m)) {
		memset(&other.m, 0, sizeof(M));
	}
	SharedBuffer<T>& operator=(SharedBuffer<T>&& other) noexcept {
		if (this != &other) {
			if(m.allocation)
				vmaUnmapMemory(vkctx.allocator, m.allocation);
			vmaDestroyBuffer(vkctx.allocator, m.buffer, m.allocation);
			m = std::move(other.m);
			memset(&other.m, 0, sizeof(M));
		}
		return *this;
	}

	static SharedBuffer<T> create(size_t count) {
		VkBufferCreateInfo buffer_info = info::buffer_create_info(sizeof(T) * count, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

		VmaAllocationCreateInfo alloc_info = {};
		alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
		alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
		alloc_info.preferredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

		VmaAllocation allocation;
		VkBuffer buffer;
		VmaAllocationInfo typed_buffer_info = {};
		VK_CHECK(vmaCreateBuffer(vkctx.allocator, &buffer_info, &alloc_info, &buffer, &allocation, &typed_buffer_info));

		void *mapped_data = 0;
		vmaMapMemory(vkctx.allocator, allocation, &mapped_data);
		memset(mapped_data, 0, sizeof(T) * count);

		VkDescriptorBufferInfo buffer_info_desc = {};
		buffer_info_desc.buffer = buffer;
		buffer_info_desc.offset = 0;
		buffer_info_desc.range = sizeof(T) * count;

		VkWriteDescriptorSet write_desc = {};
		write_desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write_desc.dstBinding = 0;
		write_desc.dstArrayElement = bindless_storage_index;
		write_desc.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		write_desc.descriptorCount = 1;
		write_desc.pBufferInfo = &buffer_info_desc;

		update_descriptor_all_frames(write_desc, bindless_storage_desc);

		return SharedBuffer<T>(M {
			.allocation = allocation,
			.buffer = buffer,
			.count = count,
			.handle = bindless_storage_index++,
			.wrap = (T*)mapped_data,
		});
	}

	uint32_t handle() const {
		return m.handle;
	}

	// Returns the mapped base pointer. const-correctness is relaxed here
	// because the buffer merely holds a pointer to mapped GPU memory that is
	// not logically part of the object's state — callers mutate the contents
	// through this pointer regardless of the buffer's const-ness.
	T *operator->() const {
		return m.wrap;
	}

	T &operator[](size_t index) const {
		return m.wrap[index];
	}
};

// A single material parameter value, addressed by the field name the *shader*
// declares in its `Material` struct. The value is stored inline (by copy) so it
// is always safe to build these as temporaries in an initializer list.
struct MaterialParam {
	std::string name;
	std::array<uint8_t, 64> storage{};
	size_t size = 0;

	static MaterialParam tex(const char *name, uint32_t value) {
		MaterialParam p; p.name = name; p.size = sizeof(value);
		std::memcpy(p.storage.data(), &value, sizeof(value));
		return p;
	}

	static MaterialParam vec4(const char *name, const glm::vec4 &value) {
		MaterialParam p; p.name = name; p.size = sizeof(value);
		std::memcpy(p.storage.data(), &value, sizeof(value));
		return p;
	}

	static MaterialParam raw(const char *name, const void *data, size_t size) {
		MaterialParam p; p.name = name; p.size = size;
		assert(size <= p.storage.size());
		std::memcpy(p.storage.data(), data, size);
		return p;
	}
};

// A host-visible buffer of `count` materials, each `stride` bytes wide. `stride`
// is whatever the owning shader's `Material` struct reflects as, so the same
// buffer indexes identically to `StructuredBuffer<Material>[i]` on the GPU.
//
// This is a thin wrapper over `SharedBuffer<uint8_t>` (which owns the Vma
// allocation, mapping, and bindless descriptor write); `MaterialBuffer` only
// adds the slot-abstraction (`stride`, `count`, `alloc`, `operator[]`).
class MaterialBuffer {
	struct M {
		SharedBuffer<uint8_t> buffer;
		size_t stride = 0;
		size_t count = 0;
		size_t next = 0;
	} m;

	explicit MaterialBuffer(M m) : m(std::move(m)) {}
public:
	MaterialBuffer() = default;
	MaterialBuffer(MaterialBuffer &&) noexcept = default;
	MaterialBuffer &operator=(MaterialBuffer &&) noexcept = default;
	MaterialBuffer(const MaterialBuffer &) = delete;
	MaterialBuffer &operator=(const MaterialBuffer &) = delete;

	static MaterialBuffer create(size_t stride, size_t count) {
		assert(stride > 0);
		return MaterialBuffer(M{
			.buffer = SharedBuffer<uint8_t>::create(stride * count),
			.stride = stride,
			.count = count,
		});
	}

	uint32_t handle() const { return m.buffer.handle(); }
	size_t stride() const { return m.stride; }
	size_t count() const { return m.count; }
	size_t used() const { return m.next; }

	uint32_t alloc() {
		assert(m.next < m.count);
		return uint32_t(m.next++);
	}

	uint8_t *operator[](size_t index) const { return &m.buffer[index * m.stride]; }
};

template<class F>
class WithResultOf {
	F&& fun;
public:
	using T = decltype(std::declval<F&&>()());
	explicit WithResultOf(F&& f) : fun(std::forward<F>(f)) {}
	operator T() { return fun(); }
};

template<class F>
inline WithResultOf<F> with_result_of(F&& f) {
	return WithResultOf<F>(std::forward<F>(f));
}
