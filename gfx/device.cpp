#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "gfx/device.h"

#include <array>
#include <stdexcept>
#include <string>

#include "gfx/command_list.h"
#include "gfx/image.h"
#include "gfx/upload_batch.h"
#include "gfx/vkinfo.h"
#include "gfx/vktools.h"

namespace gfx {
namespace {

VkDescriptorSetLayout create_bindless_layout(
	vkb::DispatchTable &dispatch,
	std::vector<VkDescriptorSetLayoutBinding> bindings
) {
	auto layout_info = info::descriptor_set_layout_info(bindings);
	layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;

	std::vector<VkDescriptorBindingFlags> flags(bindings.size());
	for(size_t index = 0; index < bindings.size(); index++) {
		flags[index] =
			VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT |
			VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT;
		if(index + 1 == bindings.size())
			flags[index] |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT;
	}

	VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags = {};
	binding_flags.sType =
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	binding_flags.bindingCount = uint32_t(flags.size());
	binding_flags.pBindingFlags = flags.data();
	layout_info.pNext = &binding_flags;

	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	VK_CHECK(dispatch.createDescriptorSetLayout(&layout_info, nullptr, &layout));
	return layout;
}

VkSampler create_sampler(
	vkb::DispatchTable &dispatch,
	VkFilter filter,
	VkSamplerAddressMode address_u,
	VkSamplerAddressMode address_v,
	bool comparison
) {
	VkSamplerCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	info.magFilter = filter;
	info.minFilter = filter;
	info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	info.addressModeU = address_u;
	info.addressModeV = address_v;
	info.addressModeW = address_v;
	info.compareEnable = comparison ? VK_TRUE : VK_FALSE;
	info.compareOp = comparison ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_ALWAYS;
	info.minLod = 0.0f;
	info.maxLod = VK_LOD_CLAMP_NONE;
	info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

	VkSampler sampler = VK_NULL_HANDLE;
	VK_CHECK(dispatch.createSampler(&info, nullptr, &sampler));
	return sampler;
}

} // namespace

Device Device::create(Instance &instance, const DeviceDesc &desc) {
	if(desc.frames_in_flight == 0)
		throw std::invalid_argument("gfx::Device::create: frames_in_flight must be greater than zero");

	VkPhysicalDeviceFeatures features = {};
	features.shaderStorageImageReadWithoutFormat = VK_TRUE;
	features.shaderStorageImageWriteWithoutFormat = VK_TRUE;

	VkPhysicalDeviceVulkan13Features features_13 = {};
	features_13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	features_13.dynamicRendering = desc.graphics ? VK_TRUE : VK_FALSE;
	features_13.synchronization2 = VK_TRUE;

	VkPhysicalDeviceVulkan12Features features_12 = {};
	features_12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	features_12.descriptorIndexing = VK_TRUE;
	const bool buffer_device_address = desc.buffer_device_address || desc.ray_tracing;
	features_12.bufferDeviceAddress = buffer_device_address ? VK_TRUE : VK_FALSE;
	features_12.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
	features_12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
	features_12.descriptorBindingPartiallyBound = VK_TRUE;
	features_12.descriptorBindingVariableDescriptorCount = VK_TRUE;
	features_12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
	features_12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	features_12.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
	features_12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
	features_12.runtimeDescriptorArray = VK_TRUE;
	features_12.shaderOutputLayer = desc.graphics ? VK_TRUE : VK_FALSE;

	VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_features = {};
	acceleration_features.sType =
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
	acceleration_features.accelerationStructure = desc.ray_tracing ? VK_TRUE : VK_FALSE;
	acceleration_features.descriptorBindingAccelerationStructureUpdateAfterBind =
		desc.ray_tracing ? VK_TRUE : VK_FALSE;

	VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_features = {};
	ray_tracing_features.sType =
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
	ray_tracing_features.rayTracingPipeline = desc.ray_tracing ? VK_TRUE : VK_FALSE;

	vkb::PhysicalDeviceSelector selector{instance.bootstrap()};
	selector
		.set_minimum_version(1, 3)
		.set_required_features(features)
		.set_required_features_12(features_12)
		.set_required_features_13(features_13);
	if(desc.ray_tracing) {
		selector
			.add_required_extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
			.add_required_extension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)
			.add_required_extension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)
			.add_required_extension_features(acceleration_features)
			.add_required_extension_features(ray_tracing_features);
	}
	if(desc.surface != VK_NULL_HANDLE)
		selector.set_surface(desc.surface);
	else
		selector.require_present(false);

	auto physical_result = selector.select();
	if(!physical_result) {
		throw std::runtime_error(
			"gfx::Device::create: " + std::string(physical_result.error().message())
		);
	}
	VkPhysicalDeviceAccelerationStructurePropertiesKHR acceleration_structure_properties = {};
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR ray_tracing_properties = {};
	if(desc.ray_tracing) {
		acceleration_structure_properties.sType =
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
		ray_tracing_properties.sType =
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
		acceleration_structure_properties.pNext = &ray_tracing_properties;
		VkPhysicalDeviceProperties2 properties = {};
		properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		properties.pNext = &acceleration_structure_properties;
		instance.dispatch().getPhysicalDeviceProperties2(
			physical_result.value().physical_device,
			&properties
		);
		acceleration_structure_properties.pNext = nullptr;
	}

	VkPhysicalDeviceShaderDrawParametersFeatures draw_features = {};
	draw_features.sType =
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
	draw_features.shaderDrawParameters = desc.graphics ? VK_TRUE : VK_FALSE;

	auto device_result = vkb::DeviceBuilder{physical_result.value()}
		.add_pNext(&draw_features)
		.build();
	if(!device_result) {
		throw std::runtime_error(
			"gfx::Device::create: " + std::string(device_result.error().message())
		);
	}
	auto device = device_result.value();
	auto dispatch = device.make_table();
	const vkb::QueueType queue_type = desc.graphics
		? vkb::QueueType::graphics
		: vkb::QueueType::compute;
	auto queue_result = device.get_queue(queue_type);
	auto family_result = device.get_queue_index(queue_type);
	if(!queue_result || !family_result) {
		vkb::destroy_device(device);
		throw std::runtime_error("gfx::Device::create: requested queue is unavailable");
	}
	VkQueue present_queue = queue_result.value();
	if(desc.surface != VK_NULL_HANDLE) {
		auto present_result = device.get_queue(vkb::QueueType::present);
		if(!present_result) {
			vkb::destroy_device(device);
			throw std::runtime_error("gfx::Device::create: present queue is unavailable");
		}
		present_queue = present_result.value();
	}

	const uint32_t descriptors_per_type =
		max_bindless_resources * desc.frames_in_flight;
	const std::vector<VkDescriptorPoolSize> pool_sizes = {
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptors_per_type},
		{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, descriptors_per_type},
		{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptors_per_type},
		{VK_DESCRIPTOR_TYPE_SAMPLER, descriptors_per_type},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, max_bindless_resources},
	};
	std::vector<VkDescriptorPoolSize> mutable_pool_sizes = pool_sizes;
	if(desc.ray_tracing)
		mutable_pool_sizes.push_back({
			VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
			descriptors_per_type,
		});
	VkDescriptorPoolCreateInfo descriptor_pool_info = {};
	descriptor_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptor_pool_info.flags =
		VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT |
		VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	descriptor_pool_info.poolSizeCount = uint32_t(mutable_pool_sizes.size());
	descriptor_pool_info.pPoolSizes = mutable_pool_sizes.data();
	descriptor_pool_info.maxSets =
		max_bindless_resources * uint32_t(pool_sizes.size()) * desc.frames_in_flight;
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
	VK_CHECK(dispatch.createDescriptorPool(
		&descriptor_pool_info,
		nullptr,
		&descriptor_pool
	));

	std::vector<VkDescriptorSetLayoutBinding> descriptor_bindings = {
		info::descriptor_set_layout_binding(
			VK_DESCRIPTOR_TYPE_SAMPLER,
			VK_SHADER_STAGE_ALL,
			0,
			max_bindless_resources
		),
		info::descriptor_set_layout_binding(
			VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			VK_SHADER_STAGE_ALL,
			2,
			max_bindless_resources
		),
		info::descriptor_set_layout_binding(
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			VK_SHADER_STAGE_ALL,
			3,
			max_bindless_resources
		),
		info::descriptor_set_layout_binding(
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			VK_SHADER_STAGE_ALL,
			7,
			max_bindless_resources
		),
	};
	if(desc.ray_tracing)
		descriptor_bindings.push_back(info::descriptor_set_layout_binding(
			VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
			VK_SHADER_STAGE_ALL,
			8,
			max_bindless_resources
		));
	VkDescriptorSetLayout descriptor_layout = create_bindless_layout(
		dispatch,
		std::move(descriptor_bindings)
	);

	std::vector<VkDescriptorSetLayout> layouts(
		desc.frames_in_flight,
		descriptor_layout
	);
	std::vector<uint32_t> variable_counts(
		desc.frames_in_flight,
		max_bindless_resources
	);
	VkDescriptorSetVariableDescriptorCountAllocateInfo variable_info = {};
	variable_info.sType =
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
	variable_info.descriptorSetCount = desc.frames_in_flight;
	variable_info.pDescriptorCounts = variable_counts.data();

	VkDescriptorSetAllocateInfo descriptor_allocate_info = {};
	descriptor_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptor_allocate_info.pNext = &variable_info;
	descriptor_allocate_info.descriptorPool = descriptor_pool;
	descriptor_allocate_info.descriptorSetCount = desc.frames_in_flight;
	descriptor_allocate_info.pSetLayouts = layouts.data();
	std::vector<VkDescriptorSet> descriptor_sets(desc.frames_in_flight);
	VK_CHECK(dispatch.allocateDescriptorSets(
		&descriptor_allocate_info,
		descriptor_sets.data()
	));

	VkPushConstantRange push_constants = {};
	push_constants.stageFlags = VK_SHADER_STAGE_ALL;
	push_constants.size = 128;
	const std::array<VkDescriptorSetLayout, 2> pipeline_descriptor_layouts = {
		descriptor_layout,
		descriptor_layout,
	};
	VkPipelineLayoutCreateInfo pipeline_layout_info = {};
	pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout_info.setLayoutCount = uint32_t(pipeline_descriptor_layouts.size());
	pipeline_layout_info.pSetLayouts = pipeline_descriptor_layouts.data();
	pipeline_layout_info.pushConstantRangeCount = 1;
	pipeline_layout_info.pPushConstantRanges = &push_constants;
	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	VK_CHECK(dispatch.createPipelineLayout(
		&pipeline_layout_info,
		nullptr,
		&pipeline_layout
	));

	VmaVulkanFunctions vma_functions = {};
	vma_functions.vkGetInstanceProcAddr = instance.bootstrap().fp_vkGetInstanceProcAddr;
	vma_functions.vkGetDeviceProcAddr = device.fp_vkGetDeviceProcAddr;
	vma_functions.vkAllocateMemory = dispatch.fp_vkAllocateMemory;
	vma_functions.vkBindBufferMemory = dispatch.fp_vkBindBufferMemory;
	vma_functions.vkBindImageMemory = dispatch.fp_vkBindImageMemory;
	vma_functions.vkCreateBuffer = dispatch.fp_vkCreateBuffer;
	vma_functions.vkCreateImage = dispatch.fp_vkCreateImage;
	vma_functions.vkDestroyBuffer = dispatch.fp_vkDestroyBuffer;
	vma_functions.vkDestroyImage = dispatch.fp_vkDestroyImage;
	vma_functions.vkFlushMappedMemoryRanges = dispatch.fp_vkFlushMappedMemoryRanges;
	vma_functions.vkFreeMemory = dispatch.fp_vkFreeMemory;
	vma_functions.vkGetBufferMemoryRequirements = dispatch.fp_vkGetBufferMemoryRequirements;
	vma_functions.vkGetImageMemoryRequirements = dispatch.fp_vkGetImageMemoryRequirements;
	vma_functions.vkGetPhysicalDeviceMemoryProperties =
		instance.dispatch().fp_vkGetPhysicalDeviceMemoryProperties;
	vma_functions.vkGetPhysicalDeviceProperties =
		instance.dispatch().fp_vkGetPhysicalDeviceProperties;
	vma_functions.vkInvalidateMappedMemoryRanges = dispatch.fp_vkInvalidateMappedMemoryRanges;
	vma_functions.vkMapMemory = dispatch.fp_vkMapMemory;
	vma_functions.vkUnmapMemory = dispatch.fp_vkUnmapMemory;
	vma_functions.vkCmdCopyBuffer = dispatch.fp_vkCmdCopyBuffer;

	VmaAllocatorCreateInfo allocator_info = {};
	if(buffer_device_address)
		allocator_info.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	allocator_info.instance = instance.native();
	allocator_info.device = device.device;
	allocator_info.physicalDevice = device.physical_device.physical_device;
	allocator_info.pVulkanFunctions = &vma_functions;
	VmaAllocator allocator = VK_NULL_HANDLE;
	VK_CHECK(vmaCreateAllocator(&allocator_info, &allocator));

	std::vector<VkSampler> samplers = {
		create_sampler(dispatch, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, false),
		create_sampler(dispatch, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false),
		create_sampler(dispatch, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, false),
		create_sampler(dispatch, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false),
		create_sampler(dispatch, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, true),
		create_sampler(dispatch, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false),
	};

	std::vector<VkDescriptorImageInfo> sampler_infos;
	sampler_infos.reserve(samplers.size());
	for(VkSampler sampler : samplers)
		sampler_infos.push_back(VkDescriptorImageInfo{.sampler = sampler});
	VkWriteDescriptorSet sampler_write = {};
	sampler_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	sampler_write.dstBinding = 0;
	sampler_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
	sampler_write.descriptorCount = uint32_t(sampler_infos.size());
	sampler_write.pImageInfo = sampler_infos.data();
	std::vector<VkWriteDescriptorSet> sampler_writes(
		descriptor_sets.size(),
		sampler_write
	);
	for(size_t index = 0; index < sampler_writes.size(); index++)
		sampler_writes[index].dstSet = descriptor_sets[index];
	dispatch.updateDescriptorSets(
		uint32_t(sampler_writes.size()),
		sampler_writes.data(),
		0,
		nullptr
	);

	return Device(M{
		.device = device,
		.dispatch = dispatch,
		.allocator = allocator,
		.queue = queue_result.value(),
		.present_queue = present_queue,
		.queue_family = family_result.value(),
		.descriptor_pool = descriptor_pool,
		.descriptor_layout = descriptor_layout,
		.pipeline_layout = pipeline_layout,
		.descriptor_sets = std::move(descriptor_sets),
		.samplers = std::move(samplers),
		.frames_in_flight = desc.frames_in_flight,
		.graphics = desc.graphics,
		.buffer_device_address = buffer_device_address,
		.ray_tracing = desc.ray_tracing,
		.acceleration_structure_properties = acceleration_structure_properties,
		.ray_tracing_properties = ray_tracing_properties,
	});
}

Device::~Device() {
	if(m.device.device == VK_NULL_HANDLE)
		return;
	m.dispatch.deviceWaitIdle();
	for(VkSampler sampler : m.samplers)
		m.dispatch.destroySampler(sampler, nullptr);
	if(m.pipeline_layout != VK_NULL_HANDLE)
		m.dispatch.destroyPipelineLayout(m.pipeline_layout, nullptr);
	if(m.descriptor_pool != VK_NULL_HANDLE)
		m.dispatch.destroyDescriptorPool(m.descriptor_pool, nullptr);
	if(m.descriptor_layout != VK_NULL_HANDLE)
		m.dispatch.destroyDescriptorSetLayout(m.descriptor_layout, nullptr);
	if(m.allocator != VK_NULL_HANDLE)
		vmaDestroyAllocator(m.allocator);
	vkb::destroy_device(m.device);
}

void Device::set_frame_index(uint32_t index) {
	if(index >= m.frames_in_flight)
		throw std::out_of_range("gfx::Device::set_frame_index: index out of range");
	m.frame_index = index;
}

void Device::wait_idle() {
	VK_CHECK(m.dispatch.deviceWaitIdle());
}

void Device::submit_and_wait(
	const std::function<void(CommandList &)> &record
) {
	auto batch = UploadBatch::create(*this);
	batch.record(record);
	auto submission = batch.submit();
	submission.wait();
}

void Device::update_all_descriptor_sets(const VkWriteDescriptorSet &write) {
	std::vector<VkWriteDescriptorSet> writes(m.descriptor_sets.size(), write);
	for(size_t index = 0; index < writes.size(); index++)
		writes[index].dstSet = m.descriptor_sets[index];
	m.dispatch.updateDescriptorSets(
		uint32_t(writes.size()),
		writes.data(),
		0,
		nullptr
	);
}

uint32_t Device::register_storage_buffer(
	VkBuffer buffer,
	VkDeviceSize size
) {
	if(m.storage_index >= max_bindless_resources)
		throw std::runtime_error("gfx: bindless storage-buffer heap exhausted");
	const uint32_t descriptor_index = m.storage_index++;

	VkDescriptorBufferInfo buffer_info = {};
	buffer_info.buffer = buffer;
	buffer_info.range = size;
	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstBinding = 7;
	write.dstArrayElement = descriptor_index;
	write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	write.descriptorCount = 1;
	write.pBufferInfo = &buffer_info;
	update_all_descriptor_sets(write);
	return descriptor_index;
}

uint32_t Device::register_acceleration_structure(
	VkAccelerationStructureKHR structure
) {
	if(!m.ray_tracing)
		throw std::runtime_error("gfx: ray tracing was not enabled for this device");
	if(structure == VK_NULL_HANDLE)
		throw std::invalid_argument("gfx: cannot register a null acceleration structure");
	if(m.acceleration_structure_index >= max_bindless_resources)
		throw std::runtime_error("gfx: bindless acceleration-structure heap exhausted");
	const uint32_t descriptor_index = m.acceleration_structure_index++;

	VkWriteDescriptorSetAccelerationStructureKHR acceleration_write = {};
	acceleration_write.sType =
		VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
	acceleration_write.accelerationStructureCount = 1;
	acceleration_write.pAccelerationStructures = &structure;
	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.pNext = &acceleration_write;
	write.dstBinding = 8;
	write.dstArrayElement = descriptor_index;
	write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	write.descriptorCount = 1;
	update_all_descriptor_sets(write);
	return descriptor_index;
}

uint32_t Device::register_image(const ImageRef &image) {
	if(m.texture_index >= max_bindless_resources)
		throw std::runtime_error("gfx: bindless image heap exhausted");
	const uint32_t descriptor_index = m.texture_index++;

	VkDescriptorImageInfo sampled_info = {};
	sampled_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	sampled_info.imageView = image.view;
	VkWriteDescriptorSet sampled_write = {};
	sampled_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	sampled_write.dstBinding = 2;
	sampled_write.dstArrayElement = descriptor_index;
	sampled_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	sampled_write.descriptorCount = 1;
	sampled_write.pImageInfo = &sampled_info;
	update_all_descriptor_sets(sampled_write);

	if((image.desc.usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0) {
		VkDescriptorImageInfo storage_info = {};
		storage_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		storage_info.imageView = image.view;
		VkWriteDescriptorSet storage_write = {};
		storage_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		storage_write.dstBinding = 3;
		storage_write.dstArrayElement = descriptor_index;
		storage_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		storage_write.descriptorCount = 1;
		storage_write.pImageInfo = &storage_info;
		update_all_descriptor_sets(storage_write);
	}
	return descriptor_index;
}

void Device::transition(
	VkCommandBuffer command,
	VkImage image,
	VkImageLayout current_layout,
	VkImageLayout new_layout,
	VkImageAspectFlags aspect_mask
) {
	VkImageMemoryBarrier2 barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
	barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	barrier.dstAccessMask =
		VK_ACCESS_2_MEMORY_WRITE_BIT |
		VK_ACCESS_2_MEMORY_READ_BIT;
	barrier.oldLayout = current_layout;
	barrier.newLayout = new_layout;
	barrier.subresourceRange = info::image_subresource_range(aspect_mask);
	barrier.image = image;

	VkDependencyInfo dependency = {};
	dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependency.imageMemoryBarrierCount = 1;
	dependency.pImageMemoryBarriers = &barrier;
	m.dispatch.cmdPipelineBarrier2(command, &dependency);
}

VkImageAspectFlags image_aspect_mask(VkFormat format) {
	switch(format) {
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_X8_D24_UNORM_PACK32:
	case VK_FORMAT_D32_SFLOAT:
		return VK_IMAGE_ASPECT_DEPTH_BIT;
	case VK_FORMAT_S8_UINT:
		return VK_IMAGE_ASPECT_STENCIL_BIT;
	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	default:
		return VK_IMAGE_ASPECT_COLOR_BIT;
	}
}

} // namespace gfx
