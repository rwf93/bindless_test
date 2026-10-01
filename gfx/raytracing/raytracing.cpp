#include "gfx/raytracing/raytracing.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "gfx/command_list.h"
#include "gfx/device.h"
#include "gfx/slang/shader.h"
#include "gfx/upload_batch.h"
#include "gfx/vkinfo.h"
#include "gfx/vktools.h"

namespace gfx::raytracing {
namespace {

VkDeviceAddress align_up(VkDeviceAddress value, VkDeviceAddress alignment) {
	if(alignment == 0)
		return value;
	return (value + alignment - 1) & ~(alignment - 1);
}

VkShaderStageFlagBits to_vk_stage(SlangStage stage) {
	switch(stage) {
	case SLANG_STAGE_RAY_GENERATION:
		return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
	case SLANG_STAGE_INTERSECTION:
		return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
	case SLANG_STAGE_ANY_HIT:
		return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
	case SLANG_STAGE_CLOSEST_HIT:
		return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
	case SLANG_STAGE_MISS:
		return VK_SHADER_STAGE_MISS_BIT_KHR;
	case SLANG_STAGE_CALLABLE:
		return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
	default:
		return VkShaderStageFlagBits(0);
	}
}

VkAccelerationStructureGeometryKHR triangle_geometry(
	const TriangleGeometry &triangle
) {
	if(triangle.vertex_address == 0 || triangle.index_address == 0)
		throw std::invalid_argument("ray-tracing triangle geometry has a null buffer address");
	if(triangle.vertex_count == 0 || triangle.index_count < 3 || triangle.index_count % 3 != 0)
		throw std::invalid_argument("ray-tracing triangle geometry has invalid counts");
	if(triangle.vertex_stride == 0)
		throw std::invalid_argument("ray-tracing triangle geometry has a zero vertex stride");

	VkAccelerationStructureGeometryKHR geometry = {};
	geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geometry.flags = triangle.flags;
	geometry.geometry.triangles.sType =
		VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	geometry.geometry.triangles.vertexFormat = triangle.vertex_format;
	geometry.geometry.triangles.vertexData.deviceAddress = triangle.vertex_address;
	geometry.geometry.triangles.vertexStride = triangle.vertex_stride;
	geometry.geometry.triangles.maxVertex = triangle.vertex_count - 1;
	geometry.geometry.triangles.indexType = triangle.index_type;
	geometry.geometry.triangles.indexData.deviceAddress = triangle.index_address;
	return geometry;
}

struct BuiltStructure {
	Device *device;
	RawBuffer storage;
	VkAccelerationStructureKHR structure = VK_NULL_HANDLE;
	VkDeviceAddress address = 0;
	~BuiltStructure() {
		if(structure != VK_NULL_HANDLE)
			device->dispatch().destroyAccelerationStructureKHR(structure, nullptr);
	}
	BuiltStructure(Device &device, RawBuffer storage, VkAccelerationStructureKHR structure, VkDeviceAddress address)
		: device(&device), storage(std::move(storage)), structure(structure), address(address) {}
	BuiltStructure(const BuiltStructure &) = delete;
	BuiltStructure &operator=(const BuiltStructure &) = delete;
	BuiltStructure(BuiltStructure &&other) noexcept
		: device(other.device), storage(std::move(other.storage)),
		  structure(std::exchange(other.structure, VK_NULL_HANDLE)), address(other.address) {}
};

BuiltStructure create_structure(
	UploadBatch &batch,
	VkAccelerationStructureTypeKHR type,
	VkBuildAccelerationStructureFlagsKHR flags,
	std::vector<VkAccelerationStructureGeometryKHR> geometries,
	std::vector<uint32_t> primitive_counts,
	VkDeviceSize scratch_alignment
) {
	Device &device = batch.device();
	if(geometries.empty() || geometries.size() != primitive_counts.size())
		throw std::invalid_argument("ray-tracing acceleration structure has no valid geometries");

	VkAccelerationStructureBuildGeometryInfoKHR build_info = {};
	build_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	build_info.type = type;
	build_info.flags = flags;
	build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build_info.geometryCount = uint32_t(geometries.size());
	build_info.pGeometries = geometries.data();

	VkAccelerationStructureBuildSizesInfoKHR size_info = {};
	size_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	device.dispatch().getAccelerationStructureBuildSizesKHR(
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&build_info,
		primitive_counts.data(),
		&size_info
	);
	if(size_info.accelerationStructureSize == 0 || size_info.buildScratchSize == 0)
		throw std::runtime_error("ray-tracing acceleration structure returned invalid build sizes");

	RawBuffer storage = RawBuffer::create(device, RawBufferDesc{
		.size = size_info.accelerationStructureSize,
		.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
		.device_address = true,
	});
	VkAccelerationStructureCreateInfoKHR create_info = {};
	create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	create_info.buffer = storage.native();
	create_info.size = size_info.accelerationStructureSize;
	create_info.type = type;
	VkAccelerationStructureKHR structure = VK_NULL_HANDLE;
	VK_CHECK(device.dispatch().createAccelerationStructureKHR(
		&create_info,
		nullptr,
		&structure
	));
	// The guard covers scratch allocation, command recording, and registration failures.
	struct StructureGuard {
		Device &device;
		VkAccelerationStructureKHR structure;
		~StructureGuard() {
			if(structure != VK_NULL_HANDLE)
				device.dispatch().destroyAccelerationStructureKHR(structure, nullptr);
		}
	} guard{device, structure};

	VkAccelerationStructureDeviceAddressInfoKHR address_info = {};
	address_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
	address_info.accelerationStructure = structure;
	const VkDeviceAddress address =
		device.dispatch().getAccelerationStructureDeviceAddressKHR(&address_info);

	if(scratch_alignment == 0)
		throw std::runtime_error("ray-tracing acceleration structure has invalid scratch alignment");
	RawBuffer scratch = RawBuffer::create(device, RawBufferDesc{
		.size = size_info.buildScratchSize + scratch_alignment - 1,
		.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		.device_address = true,
	});
	build_info.dstAccelerationStructure = structure;
	build_info.scratchData.deviceAddress = align_up(scratch.address(), scratch_alignment);

	std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges;
	ranges.reserve(primitive_counts.size());
	for(uint32_t count : primitive_counts)
		ranges.push_back(VkAccelerationStructureBuildRangeInfoKHR{
			.primitiveCount = count,
			.primitiveOffset = 0,
			.firstVertex = 0,
			.transformOffset = 0,
		});
	std::vector<const VkAccelerationStructureBuildRangeInfoKHR *> range_pointers;
	range_pointers.reserve(ranges.size());
	for(const auto &range : ranges)
		range_pointers.push_back(&range);

	batch.hold(std::move(scratch));
	batch.record([&](CommandList &commands) {
		device.dispatch().cmdBuildAccelerationStructuresKHR(
			commands.native(), 1, &build_info, range_pointers.data()
		);
	});
	batch.barrier(
		VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
	);
	guard.structure = VK_NULL_HANDLE;
	return BuiltStructure(device, std::move(storage), structure, address);
}

} // namespace

AccelerationStructure AccelerationStructure::record_bottom_level(
	UploadBatch &batch,
	const BottomLevelDesc &desc
) {
	Device &device = batch.device();
	if(desc.triangles.empty())
		throw std::invalid_argument("AccelerationStructure::record_bottom_level: no triangles");
	std::vector<VkAccelerationStructureGeometryKHR> geometries;
	std::vector<uint32_t> primitive_counts;
	geometries.reserve(desc.triangles.size());
	primitive_counts.reserve(desc.triangles.size());
	for(const TriangleGeometry &triangle : desc.triangles) {
		geometries.push_back(triangle_geometry(triangle));
		primitive_counts.push_back(triangle.index_count / 3);
	}
	auto built = create_structure(
		batch,
		VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		desc.flags,
		std::move(geometries),
		std::move(primitive_counts),
		device.acceleration_structure_properties().minAccelerationStructureScratchOffsetAlignment
	);
	auto result = AccelerationStructure(M{
		.device = &device,
		.storage = std::move(built.storage),
		.structure = built.structure,
		.address = built.address,
		.handle = UINT32_MAX,
	});
	built.structure = VK_NULL_HANDLE;
	return result;
}

AccelerationStructure AccelerationStructure::record_top_level(
	UploadBatch &batch,
	const TopLevelDesc &desc
) {
	Device &device = batch.device();
	if(desc.instances.empty())
		throw std::invalid_argument("AccelerationStructure::record_top_level: no instances");
	if(desc.instances.size() > UINT32_MAX)
		throw std::invalid_argument("AccelerationStructure::record_top_level: too many instances");
	RawBuffer instance_buffer = RawBuffer::create(device, RawBufferDesc{
		.size = desc.instances.size_bytes(),
		.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.device_address = true,
	});
	batch.upload(instance_buffer, std::as_bytes(desc.instances));

	VkAccelerationStructureGeometryKHR geometry = {};
	geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geometry.geometry.instances.sType =
		VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	geometry.geometry.instances.arrayOfPointers = VK_FALSE;
	geometry.geometry.instances.data.deviceAddress = instance_buffer.address();
	batch.hold(std::move(instance_buffer));

	auto built = create_structure(
		batch,
		VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		desc.flags,
		{geometry},
		{uint32_t(desc.instances.size())},
		device.acceleration_structure_properties().minAccelerationStructureScratchOffsetAlignment
	);
	const uint32_t handle = device.register_acceleration_structure(built.structure);
	auto result = AccelerationStructure(M{
		.device = &device,
		.storage = std::move(built.storage),
		.structure = built.structure,
		.address = built.address,
		.handle = handle,
	});
	built.structure = VK_NULL_HANDLE;
	return result;
}

void AccelerationStructure::destroy() {
	if(m.structure != VK_NULL_HANDLE) {
		m.device->dispatch().destroyAccelerationStructureKHR(m.structure, nullptr);
		m.structure = VK_NULL_HANDLE;
	}
	m.device = nullptr;
	m.address = 0;
	m.handle = UINT32_MAX;
}

AccelerationStructure::~AccelerationStructure() {
	destroy();
}

AccelerationStructure::AccelerationStructure(
	AccelerationStructure &&other
) noexcept : m(std::move(other.m)) {
	other.m.device = nullptr;
	other.m.structure = VK_NULL_HANDLE;
	other.m.address = 0;
	other.m.handle = UINT32_MAX;
}

AccelerationStructure &AccelerationStructure::operator=(
	AccelerationStructure &&other
) noexcept {
	if(this != &other) {
		destroy();
		m.device = other.m.device;
		m.storage = std::move(other.m.storage);
		m.structure = other.m.structure;
		m.address = other.m.address;
		m.handle = other.m.handle;
		other.m.device = nullptr;
		other.m.structure = VK_NULL_HANDLE;
		other.m.address = 0;
		other.m.handle = UINT32_MAX;
	}
	return *this;
}

Pipeline Pipeline::create(
	Device &device,
	SlangProgram &program,
	const PipelineDesc &desc
) {
	if(!device.ray_tracing_enabled())
		throw std::runtime_error("ray-tracing pipeline requires DeviceDesc::ray_tracing");
	if(desc.max_recursion_depth == 0)
		throw std::invalid_argument("ray-tracing pipeline recursion depth must be greater than zero");
	const VkPhysicalDeviceRayTracingPipelinePropertiesKHR &properties =
		device.ray_tracing_properties();
	if(desc.max_recursion_depth > properties.maxRayRecursionDepth)
		throw std::invalid_argument("ray-tracing pipeline recursion depth exceeds device limit");
	if(properties.shaderGroupHandleSize == 0 ||
		properties.shaderGroupHandleAlignment == 0 ||
		properties.shaderGroupBaseAlignment == 0)
	{
		throw std::runtime_error("ray-tracing pipeline returned invalid shader-group properties");
	}

	auto layout = program.component()->getLayout();
	struct ReflectedEntryPoint {
		uint32_t entry_point_index;
		SlangStage stage;
	};
	std::vector<VkPipelineShaderStageCreateInfo> stages;
	std::vector<uint32_t> stage_indices(layout->getEntryPointCount(), VK_SHADER_UNUSED_KHR);
	std::vector<ReflectedEntryPoint> reflected;
	for(uint32_t index = 0; index < layout->getEntryPointCount(); index++) {
		auto entry = layout->getEntryPointByIndex(index);
		VkShaderStageFlagBits stage_flag = to_vk_stage(entry->getStage());
		if(stage_flag == 0)
			continue;
		VkPipelineShaderStageCreateInfo stage = {};
		stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage.stage = stage_flag;
		stage.pName = entry->getName();
		stage_indices[index] = uint32_t(stages.size());
		stages.push_back(stage);
		reflected.push_back({
			.entry_point_index = index,
			.stage = entry->getStage(),
		});
	}

	auto collect = [&](SlangStage stage) {
		std::vector<uint32_t> result;
		for(const ReflectedEntryPoint &entry : reflected) {
			if(entry.stage == stage)
				result.push_back(entry.entry_point_index);
		}
		return result;
	};
	const std::vector<uint32_t> raygen_entries = collect(SLANG_STAGE_RAY_GENERATION);
	const std::vector<uint32_t> miss_entries = collect(SLANG_STAGE_MISS);
	const std::vector<uint32_t> any_hit_entries = collect(SLANG_STAGE_ANY_HIT);
	const std::vector<uint32_t> closest_hit_entries = collect(SLANG_STAGE_CLOSEST_HIT);
	const std::vector<uint32_t> intersection_entries = collect(SLANG_STAGE_INTERSECTION);
	const std::vector<uint32_t> callable_entries = collect(SLANG_STAGE_CALLABLE);
	if(raygen_entries.size() != 1)
		throw std::invalid_argument(
			"ray-tracing pipeline requires exactly one reflected ray-generation entry point"
		);

	auto reflected_name = [&](uint32_t entry_point_index) {
		return std::string(
			layout->getEntryPointByIndex(entry_point_index)->getName()
		);
	};
	auto find_stage = [&](std::string_view name, SlangStage expected) -> uint32_t {
		for(uint32_t index = 0; index < layout->getEntryPointCount(); index++) {
			auto entry = layout->getEntryPointByIndex(index);
			if(name == entry->getName()) {
				if(entry->getStage() != expected)
					throw std::invalid_argument("ray-tracing entry point has the wrong shader stage: " + std::string(name));
				return stage_indices[index];
			}
		}
		throw std::invalid_argument("ray-tracing entry point was not found: " + std::string(name));
	};

	std::vector<HitGroupDesc> hit_groups = desc.hit_groups;
	if(hit_groups.empty()) {
		if(any_hit_entries.size() > 1 ||
			closest_hit_entries.size() > 1 ||
			intersection_entries.size() > 1)
		{
			throw std::invalid_argument(
				"multiple reflected hit shaders require explicit ray-tracing hit groups"
			);
		}
		if(!any_hit_entries.empty() ||
			!closest_hit_entries.empty() ||
			!intersection_entries.empty())
		{
			HitGroupDesc inferred;
			if(!closest_hit_entries.empty())
				inferred.closest_hit = reflected_name(closest_hit_entries.front());
			if(!any_hit_entries.empty())
				inferred.any_hit = reflected_name(any_hit_entries.front());
			if(!intersection_entries.empty())
				inferred.intersection = reflected_name(intersection_entries.front());
			hit_groups.push_back(std::move(inferred));
		}
	}

	std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
	groups.reserve(
		1 + miss_entries.size() + hit_groups.size() + callable_entries.size()
	);
	groups.push_back(VkRayTracingShaderGroupCreateInfoKHR{
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
		.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
		.generalShader = stage_indices[raygen_entries.front()],
		.closestHitShader = VK_SHADER_UNUSED_KHR,
		.anyHitShader = VK_SHADER_UNUSED_KHR,
		.intersectionShader = VK_SHADER_UNUSED_KHR,
	});
	for(uint32_t entry_point_index : miss_entries)
		groups.push_back(VkRayTracingShaderGroupCreateInfoKHR{
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader = stage_indices[entry_point_index],
			.closestHitShader = VK_SHADER_UNUSED_KHR,
			.anyHitShader = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR,
		});
	for(const HitGroupDesc &hit : hit_groups) {
		if(hit.closest_hit.empty() && hit.any_hit.empty() && hit.intersection.empty())
			throw std::invalid_argument("ray-tracing hit group is empty");
		groups.push_back(VkRayTracingShaderGroupCreateInfoKHR{
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = hit.intersection.empty()
				? VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR
				: VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR,
			.generalShader = VK_SHADER_UNUSED_KHR,
			.closestHitShader = hit.closest_hit.empty()
				? VK_SHADER_UNUSED_KHR
				: find_stage(hit.closest_hit, SLANG_STAGE_CLOSEST_HIT),
			.anyHitShader = hit.any_hit.empty()
				? VK_SHADER_UNUSED_KHR
				: find_stage(hit.any_hit, SLANG_STAGE_ANY_HIT),
			.intersectionShader = hit.intersection.empty()
				? VK_SHADER_UNUSED_KHR
				: find_stage(hit.intersection, SLANG_STAGE_INTERSECTION),
		});
	}
	for(uint32_t entry_point_index : callable_entries)
		groups.push_back(VkRayTracingShaderGroupCreateInfoKHR{
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader = stage_indices[entry_point_index],
			.closestHitShader = VK_SHADER_UNUSED_KHR,
			.anyHitShader = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR,
		});

	auto code = program.spirv();
	VkShaderModuleCreateInfo module_info = {};
	module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module_info.codeSize = code->getBufferSize();
	module_info.pCode = reinterpret_cast<const uint32_t *>(code->getBufferPointer());
	VkShaderModule module = VK_NULL_HANDLE;
	VK_CHECK(device.dispatch().createShaderModule(&module_info, nullptr, &module));
	for(auto &stage : stages)
		stage.module = module;

	VkRayTracingPipelineCreateInfoKHR pipeline_info = {};
	pipeline_info.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
	pipeline_info.stageCount = uint32_t(stages.size());
	pipeline_info.pStages = stages.data();
	pipeline_info.groupCount = uint32_t(groups.size());
	pipeline_info.pGroups = groups.data();
	pipeline_info.maxPipelineRayRecursionDepth = desc.max_recursion_depth;
	pipeline_info.layout = device.pipeline_layout();
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkResult result = device.dispatch().createRayTracingPipelinesKHR(
		VK_NULL_HANDLE,
		VK_NULL_HANDLE,
		1,
		&pipeline_info,
		nullptr,
		&pipeline
	);
	device.dispatch().destroyShaderModule(module, nullptr);
	VK_CHECK(result);
	struct PipelineGuard {
		Device &device;
		VkPipeline pipeline;
		~PipelineGuard() {
			if(pipeline != VK_NULL_HANDLE)
				device.dispatch().destroyPipeline(pipeline, nullptr);
		}
	} pipeline_guard{device, pipeline};

	const VkDeviceAddress record_stride = align_up(
		properties.shaderGroupHandleSize,
		properties.shaderGroupHandleAlignment
	);
	if(record_stride > properties.maxShaderGroupStride)
		throw std::runtime_error("ray-tracing shader binding table stride exceeds device limit");
	const uint32_t raygen_count = 1;
	const uint32_t miss_count = uint32_t(miss_entries.size());
	const uint32_t hit_count = uint32_t(hit_groups.size());
	const uint32_t callable_count = uint32_t(callable_entries.size());
	const VkDeviceAddress raygen_offset = 0;
	const VkDeviceAddress miss_offset = align_up(
		raygen_offset + raygen_count * record_stride, properties.shaderGroupBaseAlignment
	);
	const VkDeviceAddress hit_offset = align_up(
		miss_offset + miss_count * record_stride, properties.shaderGroupBaseAlignment
	);
	const VkDeviceAddress callable_offset = align_up(
		hit_offset + hit_count * record_stride, properties.shaderGroupBaseAlignment
	);
	const VkDeviceAddress sbt_size = callable_offset + callable_count * record_stride;
	std::vector<uint8_t> handles(size_t(properties.shaderGroupHandleSize) * groups.size());
	VK_CHECK(device.dispatch().getRayTracingShaderGroupHandlesKHR(
		pipeline,
		0,
		uint32_t(groups.size()),
		handles.size(),
		handles.data()
	));
	std::vector<std::byte> sbt_data(static_cast<size_t>(sbt_size), std::byte{});
	auto copy_records = [&](VkDeviceAddress offset, uint32_t first_group, uint32_t count) {
		for(uint32_t index = 0; index < count; index++) {
			std::memcpy(
				sbt_data.data() + offset + index * record_stride,
				handles.data() + size_t(first_group + index) * properties.shaderGroupHandleSize,
				properties.shaderGroupHandleSize
			);
		}
	};
	copy_records(raygen_offset, 0, raygen_count);
	copy_records(miss_offset, raygen_count, miss_count);
	copy_records(hit_offset, raygen_count + miss_count, hit_count);
	copy_records(callable_offset, raygen_count + miss_count + hit_count, callable_count);

	RawBuffer sbt = RawBuffer::create(device, RawBufferDesc{
		.size = sbt_size + properties.shaderGroupBaseAlignment - 1,
		.usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR,
		.host_visible = true,
		.device_address = true,
	});
	const VkDeviceAddress sbt_address = align_up(
		sbt.address(), properties.shaderGroupBaseAlignment
	);
	sbt.write(sbt_data, sbt_address - sbt.address());
	auto result_pipeline = Pipeline(M{
		.device = &device,
		.pipeline = pipeline,
		.shader_binding_table = std::move(sbt),
		.raygen_region = {
			.deviceAddress = sbt_address + raygen_offset,
			.stride = record_stride,
			.size = raygen_count * record_stride,
		},
		.miss_region = {
			.deviceAddress = miss_count == 0 ? 0 : sbt_address + miss_offset,
			.stride = miss_count == 0 ? 0 : record_stride,
			.size = miss_count * record_stride,
		},
		.hit_region = {
			.deviceAddress = hit_count == 0 ? 0 : sbt_address + hit_offset,
			.stride = hit_count == 0 ? 0 : record_stride,
			.size = hit_count * record_stride,
		},
		.callable_region = {
			.deviceAddress = callable_count == 0 ? 0 : sbt_address + callable_offset,
			.stride = callable_count == 0 ? 0 : record_stride,
			.size = callable_count * record_stride,
		},
	});
	pipeline_guard.pipeline = VK_NULL_HANDLE;
	return result_pipeline;
}

void Pipeline::destroy() {
	if(m.pipeline != VK_NULL_HANDLE) {
		m.device->dispatch().destroyPipeline(m.pipeline, nullptr);
		m.pipeline = VK_NULL_HANDLE;
	}
	m.device = nullptr;
}

Pipeline::~Pipeline() {
	destroy();
}

Pipeline::Pipeline(Pipeline &&other) noexcept : m(std::move(other.m)) {
	other.m.device = nullptr;
	other.m.pipeline = VK_NULL_HANDLE;
}

Pipeline &Pipeline::operator=(Pipeline &&other) noexcept {
	if(this != &other) {
		destroy();
		m.device = other.m.device;
		m.pipeline = other.m.pipeline;
		m.shader_binding_table = std::move(other.m.shader_binding_table);
		m.raygen_region = other.m.raygen_region;
		m.miss_region = other.m.miss_region;
		m.hit_region = other.m.hit_region;
		m.callable_region = other.m.callable_region;
		other.m.device = nullptr;
		other.m.pipeline = VK_NULL_HANDLE;
		other.m.raygen_region = {};
		other.m.miss_region = {};
		other.m.hit_region = {};
		other.m.callable_region = {};
	}
	return *this;
}

void Pipeline::bind(CommandList &commands) const {
	commands.set_pipeline(m.pipeline, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR);
}

void Pipeline::trace_rays(
	CommandList &commands,
	uint32_t width,
	uint32_t height,
	uint32_t depth
) const {
	if(width == 0 || height == 0 || depth == 0)
		throw std::invalid_argument("ray-tracing dispatch dimensions must be greater than zero");
	bind(commands);
	commands.trace_rays(
		m.raygen_region,
		m.miss_region,
		m.hit_region,
		m.callable_region,
		width,
		height,
		depth
	);
}

} // namespace gfx::raytracing
