#include "gfx/raw_buffer.h"

#include <cstring>
#include <stdexcept>

#include "gfx/device.h"
#include "gfx/vktools.h"

namespace gfx {

RawBuffer RawBuffer::create(Device &device, const RawBufferDesc &desc) {
	if(desc.size == 0 || desc.usage == 0)
		throw std::invalid_argument("RawBuffer::create: size and usage must be nonzero");
	if(desc.host_coherent && !desc.host_visible)
		throw std::invalid_argument("RawBuffer::create: coherent memory must be host visible");
	if(desc.device_address && !device.buffer_device_address_enabled())
		throw std::invalid_argument("RawBuffer::create: device addresses require an enabled device feature");

	VkBufferCreateInfo buffer_info{};
	buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_info.size = desc.size;
	buffer_info.usage = desc.usage |
		(desc.device_address ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT : 0);
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocation_info{};
	allocation_info.usage = desc.host_visible
		? VMA_MEMORY_USAGE_AUTO_PREFER_HOST
		: VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
	if(desc.host_visible) {
		allocation_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
			VMA_ALLOCATION_CREATE_MAPPED_BIT;
		if(desc.host_coherent)
			allocation_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	}
	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	VmaAllocationInfo result{};
	VK_CHECK(vmaCreateBuffer(
		device.allocator(), &buffer_info, &allocation_info,
		&buffer, &allocation, &result
	));
	VkDeviceAddress address = 0;
	if(desc.device_address) {
		VkBufferDeviceAddressInfo address_info{};
		address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
		address_info.buffer = buffer;
		address = device.dispatch().getBufferDeviceAddress(&address_info);
		if(address == 0) {
			vmaDestroyBuffer(device.allocator(), buffer, allocation);
			throw std::runtime_error("RawBuffer::create: device address is zero");
		}
	}
	return RawBuffer(M{
		.device = &device,
		.buffer = buffer,
		.allocation = allocation,
		.address = address,
		.size = desc.size,
		.usage = buffer_info.usage,
		.mapped = result.pMappedData,
	});
}

void RawBuffer::write(std::span<const std::byte> bytes, VkDeviceSize offset) {
	if(!m.mapped || offset > m.size || bytes.size_bytes() > m.size - offset)
		throw std::invalid_argument("RawBuffer::write: unmapped buffer or invalid range");
	std::memcpy(static_cast<std::byte *>(m.mapped) + offset, bytes.data(), bytes.size_bytes());
	VK_CHECK(vmaFlushAllocation(m.device->allocator(), m.allocation, offset, bytes.size_bytes()));
}

void RawBuffer::invalidate() const {
	if(!m.mapped)
		throw std::logic_error("RawBuffer::invalidate: buffer is not mapped");
	VK_CHECK(vmaInvalidateAllocation(m.device->allocator(), m.allocation, 0, VK_WHOLE_SIZE));
}

void RawBuffer::destroy() noexcept {
	if(m.buffer != VK_NULL_HANDLE)
		vmaDestroyBuffer(m.device->allocator(), m.buffer, m.allocation);
	m = {};
}

RawBuffer::~RawBuffer() { destroy(); }
RawBuffer::RawBuffer(RawBuffer &&other) noexcept : m(std::move(other.m)) { other.m = {}; }
RawBuffer &RawBuffer::operator=(RawBuffer &&other) noexcept {
	if(this != &other) {
		destroy();
		m = std::move(other.m);
		other.m = {};
	}
	return *this;
}

} // namespace gfx
