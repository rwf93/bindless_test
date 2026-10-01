#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <functional>
#include <span>
#include <utility>
#include <vector>

#include "gfx/raw_buffer.h"

namespace gfx {

class CommandList;
class Device;

// A submitted command buffer owns its staging and scratch allocations until wait().
class Submission {
	struct M {
		Device *device = nullptr;
		VkCommandPool pool = VK_NULL_HANDLE;
		VkFence fence = VK_NULL_HANDLE;
		std::vector<RawBuffer> temporary_buffers;
	} m;
	explicit Submission(M m) : m(std::move(m)) {}
	friend class UploadBatch;
	void destroy() noexcept;

public:
	~Submission();
	Submission(const Submission &) = delete;
	Submission &operator=(const Submission &) = delete;
	Submission(Submission &&other) noexcept;
	Submission &operator=(Submission &&other) noexcept;
	// Nonblocking completion check; releases temporary allocations when signaled.
	bool poll();
	void wait();
};

// Records uploads and dependent GPU commands in submission order. Destination
// resources must outlive the returned Submission; handles are not ready for
// use until its work is synchronized with the consuming queue or wait()ed.
class UploadBatch {
	struct M {
		Device *device = nullptr;
		VkCommandPool pool = VK_NULL_HANDLE;
		VkCommandBuffer command = VK_NULL_HANDLE;
		std::vector<RawBuffer> temporary_buffers;
	} m;
	explicit UploadBatch(M m) : m(std::move(m)) {}
	void destroy() noexcept;

public:
	~UploadBatch();
	UploadBatch(const UploadBatch &) = delete;
	UploadBatch &operator=(const UploadBatch &) = delete;
	UploadBatch(UploadBatch &&other) noexcept;
	UploadBatch &operator=(UploadBatch &&other) noexcept;

	static UploadBatch create(Device &device);
	Device &device() const { return *m.device; }
	void upload(const RawBuffer &destination, std::span<const std::byte> bytes);
	void hold(RawBuffer temporary);
	void record(const std::function<void(CommandList &)> &function);
	void barrier(
		VkPipelineStageFlags2 source_stage,
		VkAccessFlags2 source_access,
		VkPipelineStageFlags2 destination_stage,
		VkAccessFlags2 destination_access
	);
	Submission submit();
};

} // namespace gfx
