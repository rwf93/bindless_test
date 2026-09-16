#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include "vktools.h"
#include "vkcontext.h"

VulkanContext vkctx;
SDL_Window *window = nullptr;
int frame_index = 0;

VkPipelineLayout global_layout = VK_NULL_HANDLE;
uint32_t bindless_storage_index = 0;
uint32_t bindless_texture_index = 0;
std::vector<VkDescriptorSet> bindless_desc;

vkb::DispatchTable &device() {
	return vkctx.dev_disp;
}

vkb::InstanceDispatchTable &instance() {
	return vkctx.inst_disp;
}

void submit_command(std::function<void(VkCommandBuffer buffer)> func) {
	VK_CHECK(device().resetFences(1, &vkctx.immediate_fence));
	VK_CHECK(device().resetCommandBuffer(vkctx.immediate_command_buffer, 0));

	VkCommandBufferBeginInfo begin_info = {};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(device().beginCommandBuffer(vkctx.immediate_command_buffer, &begin_info));

	func(vkctx.immediate_command_buffer);

	VK_CHECK(device().endCommandBuffer(vkctx.immediate_command_buffer));

	auto command_info = info::command_buffer_submit_info(vkctx.immediate_command_buffer);
	auto submit_info = info::submit_info(&command_info, nullptr, nullptr);

	VK_CHECK(device().queueSubmit2(vkctx.device.get_queue(vkb::QueueType::graphics).value(), 1, &submit_info, vkctx.immediate_fence));
	VK_CHECK(device().waitForFences(1, &vkctx.immediate_fence, true, UINT64_MAX));
};

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

void transition(
	VkCommandBuffer command,
	VkImage image,
	VkImageLayout current_layout,
	VkImageLayout new_layout,
	VkImageAspectFlags aspect_mask
) {
	VkImageMemoryBarrier2 image_barrier = {};
	image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	image_barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
	image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	image_barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
	image_barrier.oldLayout = current_layout;
	image_barrier.newLayout = new_layout;
	image_barrier.subresourceRange = info::image_subresource_range(aspect_mask);
	image_barrier.image = image;

	VkDependencyInfo dependency_info = {};
	dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependency_info.imageMemoryBarrierCount = 1;
	dependency_info.pImageMemoryBarriers = &image_barrier;

	device().cmdPipelineBarrier2(command, &dependency_info);
}

VkDescriptorSetLayout create_bindless_desc_layout(std::vector<VkDescriptorSetLayoutBinding> bindings) {

	auto layout_info = info::descriptor_set_layout_info(bindings);
	layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;

	std::vector<VkDescriptorBindingFlags> flags(bindings.size());
	for(size_t i = 0; i < bindings.size(); i++) {
		flags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT;
		if(i == bindings.size() - 1)
			flags[i] |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT;
	}

	VkDescriptorSetLayoutBindingFlagsCreateInfoEXT binding_flags_info = {};
	binding_flags_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
	binding_flags_info.bindingCount = bindings.size();
	binding_flags_info.pBindingFlags = flags.data();

	layout_info.pNext = &binding_flags_info;

	VkDescriptorSetLayout layout;
	VK_CHECK(device().createDescriptorSetLayout(&layout_info, nullptr, &layout));

	return layout;
}

void create_bindless_desc(std::vector<VkDescriptorSetLayout> set_layouts, std::vector<VkDescriptorSet> *bindless_desc) {
	std::vector<uint32_t> max_binding(set_layouts.size());
	for (size_t i = 0; i < set_layouts.size(); i++) {
		max_binding[i] = max_bindings;
	}

	bindless_desc->resize(vkctx.max_frames);

	VkDescriptorSetAllocateInfo set_alloc_info = info::descriptor_set_allocate_info(set_layouts, vkctx.global_bindless_pool);
	VkDescriptorSetVariableDescriptorCountAllocateInfoEXT count_info = {};
	count_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT;
	count_info.descriptorSetCount = set_layouts.size();
	count_info.pDescriptorCounts = max_binding.data();

	set_alloc_info.pNext = &count_info;

	for (int f = 0; f < vkctx.max_frames; f++)
		VK_CHECK(device().allocateDescriptorSets(&set_alloc_info, &(*bindless_desc)[f]));
}

void update_descriptor_all_frames(VkWriteDescriptorSet write, const std::vector<VkDescriptorSet> &sets) {
	std::vector<VkWriteDescriptorSet> writes(sets.size(), write);
	for (size_t i = 0; i < sets.size(); i++)
		writes[i].dstSet = sets[i];
	device().updateDescriptorSets(writes.size(), writes.data(), 0, nullptr);
}

void create_vk_shit() {
	vkctx.instance = vkb::InstanceBuilder{}
						.set_app_name("asd")
						.set_engine_name("asd")
						.require_api_version(VK_API_VERSION_1_3)
						//.request_validation_layers()
						.use_default_debug_messenger()
						.enable_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)
						.build().value();

	vkctx.inst_disp = vkctx.instance.make_table();

	VK_CHECK_HANDLE(vkctx.instance.instance);

	if(!SDL_Vulkan_CreateSurface(window, vkctx.instance, nullptr, &vkctx.surface)) {
		spdlog::error("Failed to make surf");
		throw std::runtime_error("fuck");
	}

	VkPhysicalDeviceFeatures features = {};
	features.fillModeNonSolid = true;
	features.geometryShader = true;
	features.tessellationShader = true;
	// Native DescriptorHandle<RWTexture*> heaps use an unknown SPIR-V image
	// format, because the concrete Vulkan view format is selected at runtime.
	features.shaderStorageImageReadWithoutFormat = true;
	features.shaderStorageImageWriteWithoutFormat = true;

	VkPhysicalDeviceVulkan13Features features_13 = {};
	features_13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	features_13.dynamicRendering = true;
	features_13.synchronization2 = true;

	VkPhysicalDeviceVulkan12Features features_12 = {};
	features_12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	features_12.bufferDeviceAddress = true;
	features_12.descriptorIndexing = true;
	features_12.descriptorBindingStorageImageUpdateAfterBind = true;
	features_12.descriptorBindingStorageBufferUpdateAfterBind = true;
	features_12.descriptorBindingPartiallyBound = true;
	features_12.descriptorBindingVariableDescriptorCount = true;
	features_12.descriptorBindingSampledImageUpdateAfterBind = true;
	features_12.shaderSampledImageArrayNonUniformIndexing = true;
	features_12.shaderStorageImageArrayNonUniformIndexing = true;
	features_12.shaderStorageBufferArrayNonUniformIndexing = true;
	features_12.runtimeDescriptorArray = true;
	features_12.shaderOutputLayer = true;

	auto selector = vkb::PhysicalDeviceSelector{vkctx.instance}
							.set_surface(vkctx.surface)
							.set_minimum_version(1, 3)
							.set_required_features_13(features_13)
							.set_required_features_12(features_12)
							.set_required_features(features)
							.select();

	VkPhysicalDeviceShaderDrawParametersFeatures draw_features = {};
	draw_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
	draw_features.shaderDrawParameters = VK_TRUE;

	vkctx.device = vkb::DeviceBuilder{selector.value()}
		.add_pNext(&draw_features)
		.build().value();

	vkctx.dev_disp = vkctx.device.make_table();

	spdlog::info("ze vuwlkan dewice ({}) is weedy to wender", vkctx.device.physical_device.name);

	VK_CHECK_HANDLE(vkctx.device.device);

	VkSurfaceFormatKHR swapchain_format = {};
	swapchain_format.format = VK_FORMAT_B8G8R8A8_UNORM;
	swapchain_format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	VkSurfaceFormatKHR swapchain_fallback_format = {};
	swapchain_fallback_format.format = VK_FORMAT_R8G8B8A8_UNORM;
	swapchain_fallback_format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	vkctx.swapchain = vkb::SwapchainBuilder{vkctx.device}
		.set_desired_format(swapchain_format)
		.add_fallback_format(swapchain_fallback_format)
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.build().value();
	vkctx.swapchain_images = vkctx.swapchain.get_images().value();
	vkctx.swapchain_views = vkctx.swapchain.get_image_views().value();
	if(vkctx.swapchain.image_format != VK_FORMAT_B8G8R8A8_UNORM &&
		vkctx.swapchain.image_format != VK_FORMAT_R8G8B8A8_UNORM)
		throw std::runtime_error("No supported UNORM swapchain surface format is available");
	spdlog::info("Using UNORM swapchain format {} with explicit scene sRGB encoding",
		int(vkctx.swapchain.image_format));

	vkctx.max_frames = vkctx.swapchain_images.size();
	vkctx.frames.resize(vkctx.max_frames);

	auto command_pool_info = info::command_pool_create_info(vkctx.device.get_queue_index(vkb::QueueType::graphics).value(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	VK_CHECK(device().createCommandPool(&command_pool_info, nullptr, &vkctx.pool));

	VkSemaphoreCreateInfo semaphore_info = {};
	semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkFenceCreateInfo fence_info = {};
	fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	auto command_allocate_info = info::command_buffer_allocate_info(vkctx.pool);
	for(int i = 0; i < vkctx.max_frames; i++) {
		VulkanContext::FrameContext *frame = &vkctx.frames.at(i);
		VK_CHECK(device().createFence(&fence_info, nullptr, &frame->fence));
		VK_CHECK(device().createSemaphore(&semaphore_info, nullptr, &frame->submit));
		VK_CHECK(device().createSemaphore(&semaphore_info, nullptr, &frame->acquire));
		VK_CHECK(device().allocateCommandBuffers(&command_allocate_info, &frame->buffer));
	}

	VK_CHECK(device().allocateCommandBuffers(&command_allocate_info, &vkctx.immediate_command_buffer));
	VK_CHECK(device().createFence(&fence_info, nullptr, &vkctx.immediate_fence));

	const uint32_t descriptors_per_type = max_bindings * vkctx.max_frames;
	std::vector<VkDescriptorPoolSize> pool = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptors_per_type },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, descriptors_per_type },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptors_per_type },
		{ VK_DESCRIPTOR_TYPE_SAMPLER, descriptors_per_type },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, max_bindings }
	};

	VkDescriptorPoolCreateInfo pool_create_info = {};
	pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_create_info.pPoolSizes = pool.data();
	pool_create_info.poolSizeCount = pool.size();
	pool_create_info.maxSets = max_bindings * pool.size() * vkctx.max_frames;
	pool_create_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT;
	VK_CHECK(device().createDescriptorPool(&pool_create_info, nullptr, &vkctx.global_bindless_pool));

	// Slang's native DescriptorHandle<T> heap with
	// BindlessDescriptorOptions::None:
	//   binding 0 = samplers
	//   binding 2 = sampled images
	//   binding 3 = storage images
	//   binding 7 = StructuredBuffer/RWStructuredBuffer
	// Keeping separate descriptor-type arrays permits overlapping slot numbers.
	auto bindless_layout = create_bindless_desc_layout({
		info::descriptor_set_layout_binding(
			VK_DESCRIPTOR_TYPE_SAMPLER,
			VK_SHADER_STAGE_ALL,
			0,
			max_bindings
		),
		info::descriptor_set_layout_binding(
			VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			VK_SHADER_STAGE_ALL,
			2,
			max_bindings
		),
		info::descriptor_set_layout_binding(
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			VK_SHADER_STAGE_ALL,
			3,
			max_bindings
		),
		info::descriptor_set_layout_binding(
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			VK_SHADER_STAGE_ALL,
			7,
			max_bindings
		),
	});

	std::vector<VkDescriptorSetLayout> set_layouts = {
		bindless_layout
	};

	auto pipeline_layout_info = info::pipeline_layout_info(set_layouts);

	create_bindless_desc({bindless_layout}, &bindless_desc);

	VkPushConstantRange range = {};
	range.offset = 0;
	range.size = 128;
	range.stageFlags = VK_SHADER_STAGE_ALL;

	pipeline_layout_info.pPushConstantRanges = &range;
	pipeline_layout_info.pushConstantRangeCount = 1;

	VK_CHECK(device().createPipelineLayout(&pipeline_layout_info, nullptr, &global_layout));

	VmaVulkanFunctions vma_functions = {};
	vma_functions.vkGetInstanceProcAddr					= vkctx.instance.fp_vkGetInstanceProcAddr;
	vma_functions.vkGetDeviceProcAddr 					= vkctx.device.fp_vkGetDeviceProcAddr;
	vma_functions.vkAllocateMemory                    	= device().fp_vkAllocateMemory;
	vma_functions.vkBindBufferMemory                  	= device().fp_vkBindBufferMemory;
	vma_functions.vkBindImageMemory                   	= device().fp_vkBindImageMemory;
	vma_functions.vkCreateBuffer                      	= device().fp_vkCreateBuffer;
	vma_functions.vkCreateImage                       	= device().fp_vkCreateImage;
	vma_functions.vkDestroyBuffer                     	= device().fp_vkDestroyBuffer;

	vma_functions.vkDestroyImage                      	= device().fp_vkDestroyImage;
	vma_functions.vkFlushMappedMemoryRanges           	= device().fp_vkFlushMappedMemoryRanges;
	vma_functions.vkFreeMemory                        	= device().fp_vkFreeMemory;
	vma_functions.vkGetBufferMemoryRequirements       	= device().fp_vkGetBufferMemoryRequirements;
	vma_functions.vkGetImageMemoryRequirements        	= device().fp_vkGetImageMemoryRequirements;
	vma_functions.vkGetPhysicalDeviceMemoryProperties 	= instance().fp_vkGetPhysicalDeviceMemoryProperties;
	vma_functions.vkGetPhysicalDeviceProperties       	= instance().fp_vkGetPhysicalDeviceProperties;
	vma_functions.vkInvalidateMappedMemoryRanges      	= device().fp_vkInvalidateMappedMemoryRanges;
	vma_functions.vkMapMemory                         	= device().fp_vkMapMemory;
	vma_functions.vkUnmapMemory                       	= device().fp_vkUnmapMemory;
	vma_functions.vkCmdCopyBuffer                     	= device().fp_vkCmdCopyBuffer;

	VmaAllocatorCreateInfo allocator_info = {};
	allocator_info.instance = vkctx.instance.instance;
	allocator_info.device = vkctx.device.device;
	allocator_info.physicalDevice = vkctx.device.physical_device.physical_device;
	allocator_info.pVulkanFunctions = &vma_functions;
	allocator_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

	VK_CHECK(vmaCreateAllocator(&allocator_info, &vkctx.allocator));

	auto create_sampler = [&](
		VkFilter filter,
		VkSamplerAddressMode address_u,
		VkSamplerAddressMode address_v,
		bool comparison = false
	) {
		VkSamplerCreateInfo sampler_info = {};
		sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sampler_info.anisotropyEnable = VK_FALSE;
		sampler_info.maxAnisotropy = vkctx.device.physical_device.properties.limits.maxSamplerAnisotropy;
		sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		sampler_info.unnormalizedCoordinates = VK_FALSE;
		sampler_info.compareEnable = comparison ? VK_TRUE : VK_FALSE;
		sampler_info.compareOp = comparison ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_ALWAYS;
		sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler_info.minLod = 0.0f;
		sampler_info.maxLod = VK_LOD_CLAMP_NONE;

		VkSampler s;
		auto si = sampler_info;
		si.magFilter = filter;
		si.minFilter = filter;
		si.addressModeU = address_u;
		si.addressModeV = address_v;
		si.addressModeW = address_v;
		VK_CHECK(device().createSampler(&si, nullptr, &s));
		return s;
	};
	auto sampler_descriptor = [&create_sampler](
		VkFilter filter,
		VkSamplerAddressMode address_u,
		VkSamplerAddressMode address_v,
		bool comparison = false
	) {
		return VkDescriptorImageInfo{
			.sampler = create_sampler(
				filter,
				address_u,
				address_v,
				comparison
			),
			.imageView = VK_NULL_HANDLE,
			.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		};
	};

	// These slots mirror the native DescriptorHandle sampler helpers in
	// shader_constants.slang. Slot 4 is a comparison sampler for depth PCF;
	// slot 5 wraps panoramic skies horizontally and clamps their poles.
	std::vector<VkDescriptorImageInfo> sampler_infos = {
		sampler_descriptor(
			VK_FILTER_LINEAR,
			VK_SAMPLER_ADDRESS_MODE_REPEAT,
			VK_SAMPLER_ADDRESS_MODE_REPEAT
		),
		sampler_descriptor(
			VK_FILTER_LINEAR,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
		),
		sampler_descriptor(
			VK_FILTER_NEAREST,
			VK_SAMPLER_ADDRESS_MODE_REPEAT,
			VK_SAMPLER_ADDRESS_MODE_REPEAT
		),
		sampler_descriptor(
			VK_FILTER_NEAREST,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
		),
		sampler_descriptor(
			VK_FILTER_LINEAR,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			true
		),
		sampler_descriptor(
			VK_FILTER_LINEAR,
			VK_SAMPLER_ADDRESS_MODE_REPEAT,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
		),
	};

	VkWriteDescriptorSet sampler_write = {};
	sampler_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	sampler_write.dstBinding = 0;
	sampler_write.dstArrayElement = 0;
	sampler_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
	sampler_write.descriptorCount = sampler_infos.size();
	sampler_write.pImageInfo = sampler_infos.data();

	update_descriptor_all_frames(sampler_write, bindless_desc);
}

void create_imgui_shit() {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	ImGui::StyleColorsDark();

	ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3, [](const char *function_name, void *unused){
		return instance().getInstanceProcAddr(function_name);
	}, nullptr);

	ImGuiStyle& style = ImGui::GetStyle();
	style.ScaleAllSizes(SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
	style.FontScaleDpi = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)

	ImGui_ImplSDL3_InitForVulkan(window);
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = vkctx.instance;
	init_info.PhysicalDevice = vkctx.device.physical_device;
	init_info.Device = vkctx.device;
	init_info.QueueFamily = vkctx.device.get_queue_index(vkb::QueueType::graphics).value();
	init_info.Queue = vkctx.device.get_queue(vkb::QueueType::graphics).value();
	//init_info.PipelineCache = g_PipelineCache;
	init_info.DescriptorPool = vkctx.global_bindless_pool;
	init_info.MinImageCount = 2;
	init_info.ImageCount = 3;
	//init_info.Allocator = vkctx.allocator;
	init_info.UseDynamicRendering = true;

	std::vector<VkFormat> attachments = {vkctx.swapchain.image_format};
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo = info::rendering_create_info(attachments, VK_FORMAT_D32_SFLOAT);
	ImGui_ImplVulkan_Init(&init_info);
}
