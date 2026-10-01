#include "gfx/upload_batch.h"

#include <stdexcept>

#include "gfx/command_list.h"
#include "gfx/device.h"
#include "gfx/vkinfo.h"
#include "gfx/vktools.h"

namespace gfx {

UploadBatch UploadBatch::create(Device &device) {
	const auto pool_info = info::command_pool_create_info(device.queue_family());
	VkCommandPool pool = VK_NULL_HANDLE;
	VK_CHECK(device.dispatch().createCommandPool(&pool_info, nullptr, &pool));
	VkCommandBuffer command = VK_NULL_HANDLE;
	try {
		const auto allocate_info = info::command_buffer_allocate_info(pool);
		VK_CHECK(device.dispatch().allocateCommandBuffers(&allocate_info, &command));
		VkCommandBufferBeginInfo begin{};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CHECK(device.dispatch().beginCommandBuffer(command, &begin));
	} catch(...) {
		device.dispatch().destroyCommandPool(pool, nullptr);
		throw;
	}
	return UploadBatch(M{.device = &device, .pool = pool, .command = command});
}

void UploadBatch::upload(const RawBuffer &destination, std::span<const std::byte> bytes) {
	if(!m.device)
		throw std::logic_error("UploadBatch::upload: batch was submitted");
	if(destination.m.device != m.device ||
		(destination.m.usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) == 0)
	{
		throw std::invalid_argument("UploadBatch::upload: destination must be a transfer destination on this device");
	}
	if(bytes.empty() || bytes.size_bytes() > destination.size())
		throw std::invalid_argument("UploadBatch::upload: invalid data size");
	auto staging = RawBuffer::create(*m.device, RawBufferDesc{
		.size = bytes.size_bytes(),
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		.host_visible = true,
	});
	staging.write(bytes);
	m.temporary_buffers.push_back(std::move(staging));
	VkBufferCopy region{.size = bytes.size_bytes()};
	m.device->dispatch().cmdCopyBuffer(
		m.command, m.temporary_buffers.back().native(), destination.native(), 1, &region
	);
	barrier(
		VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT
	);
}

void UploadBatch::hold(RawBuffer temporary) {
	if(!m.device)
		throw std::logic_error("UploadBatch::hold: batch was submitted");
	if(temporary.m.device != m.device)
		throw std::invalid_argument("UploadBatch::hold: buffer belongs to another device");
	m.temporary_buffers.push_back(std::move(temporary));
}

void UploadBatch::record(const std::function<void(CommandList &)> &function) {
	if(!m.device)
		throw std::logic_error("UploadBatch::record: batch was submitted");
	auto commands = CommandList::create(
		*m.device, m.command,
		m.device->graphics_capable()
			? VK_PIPELINE_BIND_POINT_GRAPHICS
			: VK_PIPELINE_BIND_POINT_COMPUTE
	);
	function(commands);
}

void UploadBatch::barrier(
	VkPipelineStageFlags2 source_stage,
	VkAccessFlags2 source_access,
	VkPipelineStageFlags2 destination_stage,
	VkAccessFlags2 destination_access
) {
	if(!m.device)
		throw std::logic_error("UploadBatch::barrier: batch was submitted");
	VkMemoryBarrier2 memory{};
	memory.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
	memory.srcStageMask = source_stage;
	memory.srcAccessMask = source_access;
	memory.dstStageMask = destination_stage;
	memory.dstAccessMask = destination_access;
	VkDependencyInfo dependency{};
	dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependency.memoryBarrierCount = 1;
	dependency.pMemoryBarriers = &memory;
	m.device->dispatch().cmdPipelineBarrier2(m.command, &dependency);
}

Submission UploadBatch::submit() {
	if(!m.device)
		throw std::logic_error("UploadBatch::submit: batch was already submitted");
	VK_CHECK(m.device->dispatch().endCommandBuffer(m.command));
	VkFenceCreateInfo fence_info{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
	VkFence fence = VK_NULL_HANDLE;
	VK_CHECK(m.device->dispatch().createFence(&fence_info, nullptr, &fence));
	try {
		auto command_info = info::command_buffer_submit_info(m.command);
		auto submit_info = info::submit_info(&command_info, nullptr, nullptr);
		VK_CHECK(m.device->dispatch().queueSubmit2(m.device->queue(), 1, &submit_info, fence));
	} catch(...) {
		m.device->dispatch().destroyFence(fence, nullptr);
		throw;
	}
	Submission result(Submission::M{
		.device = m.device,
		.pool = m.pool,
		.fence = fence,
		.temporary_buffers = std::move(m.temporary_buffers),
	});
	m.device = nullptr;
	m.pool = VK_NULL_HANDLE;
	m.command = VK_NULL_HANDLE;
	return result;
}

void UploadBatch::destroy() noexcept {
	// An unsubmitted batch has no GPU work in flight.
	m.temporary_buffers.clear();
	if(m.pool != VK_NULL_HANDLE)
		m.device->dispatch().destroyCommandPool(m.pool, nullptr);
	m = {};
}

UploadBatch::~UploadBatch() { destroy(); }
UploadBatch::UploadBatch(UploadBatch &&other) noexcept : m(std::move(other.m)) { other.m = {}; }
UploadBatch &UploadBatch::operator=(UploadBatch &&other) noexcept {
	if(this != &other) {
		destroy();
		m = std::move(other.m);
		other.m = {};
	}
	return *this;
}

bool Submission::poll() {
	if(m.fence == VK_NULL_HANDLE)
		return true;
	const VkResult status = m.device->dispatch().getFenceStatus(m.fence);
	if(status == VK_NOT_READY)
		return false;
	VK_CHECK(status);
	wait();
	return true;
}

void Submission::wait() {
	if(m.fence == VK_NULL_HANDLE)
		return;
	VK_CHECK(m.device->dispatch().waitForFences(1, &m.fence, VK_TRUE, UINT64_MAX));
	m.temporary_buffers.clear();
	m.device->dispatch().destroyFence(m.fence, nullptr);
	m.device->dispatch().destroyCommandPool(m.pool, nullptr);
	m = {};
}

void Submission::destroy() noexcept {
	if(m.fence != VK_NULL_HANDLE) {
		const VkResult result = m.device->dispatch().waitForFences(
			1, &m.fence, VK_TRUE, UINT64_MAX
		);
		if(result != VK_SUCCESS)
			m.device->dispatch().deviceWaitIdle();
		m.temporary_buffers.clear();
		m.device->dispatch().destroyFence(m.fence, nullptr);
		m.device->dispatch().destroyCommandPool(m.pool, nullptr);
	}
	m = {};
}

Submission::~Submission() { destroy(); }
Submission::Submission(Submission &&other) noexcept : m(std::move(other.m)) { other.m = {}; }
Submission &Submission::operator=(Submission &&other) noexcept {
	if(this != &other) {
		destroy();
		m = std::move(other.m);
		other.m = {};
	}
	return *this;
}

} // namespace gfx
