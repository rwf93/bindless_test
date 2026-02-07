#include <spdlog/spdlog.h>
#include <vulkan/vulkan.h>
#include <VkBootstrap.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/common.hpp>
#include <glm/gtc/random.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <slang.h>
#include <slang-com-ptr.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "camera.h"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <ImGuizmo.h>
#include <imgui_internal.h>

#include "vktools.h"
#include "vkinfo.h"

static SDL_Window *window;
static SDL_Event event;

static int frame_index;
static uint32_t image_index;

static bool quit = false;

glm::mat4 calculate_model_matrix(glm::vec3 translation, glm::vec3 rotation, glm::vec3 scale) {
	glm::mat4 translation_matrix = glm::translate(glm::mat4(1.0f), translation);
	glm::mat4 rotation_matrix = glm::mat4(glm::quat(rotation));
	glm::mat4 scale_matrix = glm::scale(glm::mat4(1.0f), scale);

	return translation_matrix * rotation_matrix * scale_matrix;
}

struct AllocatedImage {
	VkImage image;
	VkImageView view;
	VmaAllocation allocation;
};
static struct VulkanContext {
	struct FrameContext {
		VkFence fence;
		VkSemaphore submit;
		VkSemaphore acquire;
		VkCommandBuffer buffer;
	};

	vkb::Instance instance;
	vkb::Device device;
	vkb::Swapchain swapchain;
	VkSurfaceKHR surface;

	std::vector<VkImage> swapchain_images;
	std::vector<VkImageView> swapchain_views;

	VkCommandPool pool;
	uint32_t max_frames;
	std::vector<FrameContext> frames;

	VkCommandBuffer immediate_command_buffer;
	VkFence immediate_fence;

	VmaAllocator allocator;

	VkDescriptorPool global_bindless_pool;

	vkb::InstanceDispatchTable inst_disp;
	vkb::DispatchTable dev_disp;

	VkSampler sampler_address_repeat;
} vkctx;

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

void transition(VkCommandBuffer command, VkImage image, VkImageLayout current_layout, VkImageLayout new_layout) {
	VkImageMemoryBarrier2 image_barrier = {};
	image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	image_barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
	image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	image_barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
	image_barrier.oldLayout = current_layout;
	image_barrier.newLayout = new_layout;
	VkImageAspectFlags aspect_mask = (new_layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL || current_layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
		? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

	image_barrier.subresourceRange = info::image_subresource_range(aspect_mask);
	image_barrier.image = image;

	VkDependencyInfo dependency_info = {};
	dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependency_info.imageMemoryBarrierCount = 1;
	dependency_info.pImageMemoryBarriers = &image_barrier;

	device().cmdPipelineBarrier2(command, &dependency_info);
}

void transition(VkImage image, VkImageLayout current_layout, VkImageLayout new_layout) {
	transition(vkctx.frames[frame_index].buffer, image, current_layout, new_layout);
}

struct PushConstants {
	uint32_t vbo_handle;
	uint32_t ibo_handle;
	uint32_t scene_handle;
	uint32_t object_handle;
	uint32_t light_handle;
	uint32_t material_handle;
	uint32_t material;
	uint32_t swapchain_write_texture_handle;
};

static VkPipelineLayout global_layout;
static uint32_t bindless_storage_index = 0;
static uint32_t bindless_texture_index = 0;

static VkDescriptorSet bindless_storage_desc;
static VkDescriptorSet bindless_texture_desc;

static constexpr uint32_t max_bindings = 2 << 12;

VkDescriptorSetLayout create_bindless_desc_layout(std::vector<VkDescriptorSetLayoutBinding> bindings) {

	auto layout_info = info::descriptor_set_layout_info(bindings);
	layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;

	VkDescriptorBindingFlags bindless_flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT;

	VkDescriptorSetLayoutBindingFlagsCreateInfoEXT binding_flags_info = {};
	binding_flags_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
	binding_flags_info.bindingCount = bindings.size();
	binding_flags_info.pBindingFlags = &bindless_flags;

	layout_info.pNext = &binding_flags_info;

	VkDescriptorSetLayout layout;
	VK_CHECK(device().createDescriptorSetLayout(&layout_info, nullptr, &layout));

	return layout;
}

void create_bindless_desc(std::vector<VkDescriptorSetLayout> set_layouts, VkDescriptorSet *bindless_desc) {
	std::vector<uint32_t> max_binding(set_layouts.size());
	for (size_t i = 0; i < set_layouts.size(); i++) {
		max_binding[i] = max_bindings;
	}

	VkDescriptorSetAllocateInfo set_alloc_info = info::descriptor_set_allocate_info(set_layouts, vkctx.global_bindless_pool);
	VkDescriptorSetVariableDescriptorCountAllocateInfoEXT count_info = {};
	count_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT;
	count_info.descriptorSetCount = set_layouts.size();
	count_info.pDescriptorCounts = max_binding.data();

	set_alloc_info.pNext = &count_info;

	VK_CHECK(device().allocateDescriptorSets(&set_alloc_info, bindless_desc));
}

class SlangProgram {
	struct M {
		static inline Slang::ComPtr<slang::IGlobalSession> global_session;
		Slang::ComPtr<slang::ISession> session;
		Slang::ComPtr<slang::IModule> module;
		Slang::ComPtr<slang::IComponentType> component;
	} m;

	explicit SlangProgram(M m) : m(std::move(m)) {};
public:
	static SlangProgram create(const char *name, const char *path) {
		if(!M::global_session.get())
			slang::createGlobalSession(M::global_session.writeRef());


		auto slang_targets = std::to_array<slang::TargetDesc>({
			{
				.format = SLANG_SPIRV,
				.profile = M::global_session->findProfile("spirv_1_4")
			}
		});


		auto slang_options = std::to_array<slang::CompilerOptionEntry>({
				{
					slang::CompilerOptionName::EmitSpirvDirectly,
					{slang::CompilerOptionValueKind::Int, 1},
				},
		});

		slang::SessionDesc session_desc = {
			.targets = slang_targets.data(),
			.targetCount = SlangInt(slang_targets.size()),
			.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR,
			.compilerOptionEntries = slang_options.data(),
			.compilerOptionEntryCount = SlangInt(slang_options.size())
		};

		Slang::ComPtr<slang::ISession> session;
		M::global_session->createSession(session_desc, session.writeRef());
		Slang::ComPtr<slang::IBlob> diagnostics;

		Slang::ComPtr<slang::IModule> module {
			session->loadModuleFromSource(name, path, nullptr, diagnostics.writeRef())
		};

		Slang::ComPtr<slang::IComponentType> component;
		module->link(component.writeRef());

		return SlangProgram(M{
			.session = std::move(session),
			.module = std::move(module),
			.component = std::move(component)
		});
	}

	Slang::ComPtr<slang::IModule> module() {
		return m.module;
	}

	Slang::ComPtr<slang::IComponentType> component() {
		return m.component;
	}

	Slang::ComPtr<ISlangBlob> blob(uint32_t index) {
		Slang::ComPtr<ISlangBlob> spirv;
		m.component->getTargetCode(index, spirv.writeRef());
		return spirv;
	}
};

static std::map<std::string, VkPipeline> pipelines;
void create_pipeline(
	std::string name,
	SlangProgram &program,
	std::vector<VkFormat> color_attachments,
	VkFormat depth_format = VK_FORMAT_UNDEFINED
) {
	VkPipelineInputAssemblyStateCreateInfo assembly_info = {};
	VkPipelineViewportStateCreateInfo viewport_info = {};
	VkPipelineRasterizationStateCreateInfo raster_info = {};
	VkPipelineMultisampleStateCreateInfo multisampling_info = {};
	VkPipelineColorBlendStateCreateInfo color_info = {};
	VkPipelineDepthStencilStateCreateInfo stencil_info = {};
	VkPipelineDynamicStateCreateInfo dynamic_info = {};
	VkPipelineVertexInputStateCreateInfo input_info = {};
	VkGraphicsPipelineCreateInfo pipeline_info = {};
	VkPipelineShaderStageCreateInfo vertex_stage = {};
	VkPipelineShaderStageCreateInfo pixel_stage = {};

	assembly_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	viewport_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	raster_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	multisampling_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	color_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	stencil_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	dynamic_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	vertex_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pixel_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

	assembly_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	assembly_info.primitiveRestartEnable = VK_FALSE;

	viewport_info.viewportCount = 1;
	viewport_info.scissorCount = 1;

	raster_info.polygonMode = VK_POLYGON_MODE_FILL;
	raster_info.lineWidth = 1.0f;
	raster_info.cullMode = VK_CULL_MODE_NONE;
	raster_info.frontFace = VK_FRONT_FACE_CLOCKWISE;

	multisampling_info.sampleShadingEnable = VK_FALSE;
	multisampling_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisampling_info.alphaToOneEnable = VK_FALSE;
	multisampling_info.alphaToCoverageEnable = VK_FALSE;

	color_info.logicOpEnable = VK_FALSE;
	color_info.logicOp = VK_LOGIC_OP_COPY;

	std::vector<VkPipelineColorBlendAttachmentState> blend_attachments(color_attachments.size());
	for (auto& blend_attachment : blend_attachments) {
		blend_attachment = {};
		blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blend_attachment.blendEnable = VK_FALSE;
	}
	color_info.attachmentCount = static_cast<uint32_t>(blend_attachments.size());
	color_info.pAttachments = blend_attachments.data();

	stencil_info.depthTestEnable = VK_TRUE;
	stencil_info.depthWriteEnable = VK_TRUE;
	stencil_info.depthCompareOp = VK_COMPARE_OP_LESS;
	stencil_info.depthBoundsTestEnable = VK_FALSE;
	stencil_info.stencilTestEnable = VK_FALSE;
	stencil_info.front = {};
	stencil_info.back = {};
	stencil_info.minDepthBounds = 0.f;
	stencil_info.maxDepthBounds = 1.f;

	std::vector<VkDynamicState> dynamic_states = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};

	dynamic_info.pDynamicStates = dynamic_states.data();
	dynamic_info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());

	auto rendering_create_info = info::rendering_create_info(color_attachments, depth_format);

	VkShaderModuleCreateInfo shader_module_info = {};
	shader_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	shader_module_info.codeSize = program.blob(0)->getBufferSize();
	shader_module_info.pCode = reinterpret_cast<const uint32_t*>(program.blob(0)->getBufferPointer());

	VkShaderModule shader_module;
	VK_CHECK(device().createShaderModule(&shader_module_info, nullptr, &shader_module));

	std::vector<VkPipelineShaderStageCreateInfo> shader_stages;

	for(auto child: program.module()->getModuleReflection()->getChildren()) {
		if(child->getKind() == slang::DeclReflection::Kind::Struct) {
			spdlog::info("struct name: {}\n", child->getName());
			for(auto struct_child: child->getChildren()) {
				spdlog::info("\t field name: {}\n", struct_child->getName());
			}
		}
	}

	vertex_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertex_stage.module = shader_module;
	vertex_stage.pName = "vertex_main";

	pixel_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	pixel_stage.module = shader_module;
	pixel_stage.pName = "fragment_main";

	shader_stages.push_back(vertex_stage);
	shader_stages.push_back(pixel_stage);

	std::vector<VkVertexInputBindingDescription> bindings = {};
	std::vector<VkVertexInputAttributeDescription> attributes = {};

	input_info = info::input_vertex_info(bindings, attributes);

	pipeline_info.pNext = &rendering_create_info;
	pipeline_info.pStages = shader_stages.data();
	pipeline_info.stageCount = static_cast<uint32_t>(shader_stages.size());
	pipeline_info.pVertexInputState = &input_info;
	pipeline_info.pInputAssemblyState = &assembly_info;
	pipeline_info.pViewportState = &viewport_info;
	pipeline_info.pRasterizationState = &raster_info;
	pipeline_info.pMultisampleState = &multisampling_info;
	pipeline_info.pColorBlendState = &color_info;
	pipeline_info.pDepthStencilState = &stencil_info;
	pipeline_info.pDynamicState = &dynamic_info;
	pipeline_info.layout = global_layout;

	VkPipeline pipeline;
	VK_CHECK(device().createGraphicsPipelines(VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline));

	pipelines[name] = pipeline;
}

void create_vk_shit() {
	vkctx.instance = vkb::InstanceBuilder{}
						.set_app_name("asd")
						.set_engine_name("asd")
						.require_api_version(VK_API_VERSION_1_3)
						.request_validation_layers()
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

	VkPhysicalDeviceVulkan13Features features_13 = {};
	features_13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	features_13.dynamicRendering = true;
	features_13.synchronization2 = true;

	VkPhysicalDeviceVulkan12Features features_12 = {};
	features_12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	features_12.bufferDeviceAddress = true;
	features_12.descriptorIndexing = true;
	features_12.descriptorBindingStorageBufferUpdateAfterBind = true;
	features_12.descriptorBindingPartiallyBound = true;
	features_12.descriptorBindingVariableDescriptorCount = true;
	features_12.descriptorBindingSampledImageUpdateAfterBind = true;
	features_12.shaderStorageBufferArrayNonUniformIndexing = true;
	features_12.runtimeDescriptorArray = true;

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

	spdlog::info("ze vuwlkan dewice ({}) is weady to wender", vkctx.device.physical_device.name);

	VK_CHECK_HANDLE(vkctx.device.device);

	VkSurfaceFormatKHR swapchain_format = {};
	swapchain_format.format = VK_FORMAT_B8G8R8A8_UNORM;
	swapchain_format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	vkctx.swapchain = vkb::SwapchainBuilder{vkctx.device}
		.set_desired_format(swapchain_format)
		.set_desired_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.build().value();
	vkctx.swapchain_images = vkctx.swapchain.get_images().value();
	vkctx.swapchain_views = vkctx.swapchain.get_image_views().value();

	vkctx.max_frames = vkctx.swapchain_images.capacity();
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

	std::vector<VkDescriptorPoolSize> pool = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, max_bindings },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, max_bindings }
	};

	VkDescriptorPoolCreateInfo pool_create_info = {};
	pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_create_info.pPoolSizes = pool.data();
	pool_create_info.poolSizeCount = pool.size();
	pool_create_info.maxSets = max_bindings * pool.size();
	pool_create_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT;
	VK_CHECK(device().createDescriptorPool(&pool_create_info, nullptr, &vkctx.global_bindless_pool));

	auto bindless_storage_layout = create_bindless_desc_layout({
		info::descriptor_set_layout_binding(
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			VK_SHADER_STAGE_ALL,
			0,
			max_bindings
		),
	});

	auto bindless_texture_layout = create_bindless_desc_layout({
		info::descriptor_set_layout_binding(
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			VK_SHADER_STAGE_ALL,
			0,
			max_bindings
		),
	});

	std::vector<VkDescriptorSetLayout> set_layouts = {
		bindless_storage_layout,
		bindless_texture_layout
	};
	auto pipeline_layout_info = info::pipeline_layout_info(set_layouts);

	create_bindless_desc({bindless_storage_layout}, &bindless_storage_desc);
	create_bindless_desc({bindless_texture_layout}, &bindless_texture_desc);

	VkPushConstantRange range = {};
	range.offset = 0;
	range.size = sizeof(PushConstants);
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

	VkSamplerCreateInfo sampler_info = {};
	sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler_info.magFilter = VK_FILTER_NEAREST;
	sampler_info.minFilter = VK_FILTER_NEAREST;
	sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler_info.anisotropyEnable = VK_FALSE;
	sampler_info.maxAnisotropy = vkctx.device.physical_device.properties.limits.maxSamplerAnisotropy;
	sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	sampler_info.unnormalizedCoordinates = VK_FALSE;
	sampler_info.compareEnable = VK_FALSE;
	sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
	sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

	device().createSampler(&sampler_info, nullptr, &vkctx.sampler_address_repeat);
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

	std::vector<VkFormat> attachments = {VK_FORMAT_B8G8R8A8_UNORM};
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo = info::rendering_create_info(attachments, VK_FORMAT_D32_SFLOAT);
	ImGui_ImplVulkan_Init(&init_info);
}

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
	) {
		auto staging_allocate_info = info::allocation_create_info();
		auto staging_buffer_info = info::buffer_create_info(1 * width * height * 4);

		VmaAllocation staging_allocation;
		VkBuffer staging_buffer;
		VK_CHECK(vmaCreateBuffer(vkctx.allocator, &staging_buffer_info, &staging_allocate_info, &staging_buffer, &staging_allocation, nullptr));

		void *mapped;
		vmaMapMemory(vkctx.allocator, staging_allocation, &mapped);
		memcpy(mapped, data, 1 * width * height * 4);
		vmaUnmapMemory(vkctx.allocator, staging_allocation);

		submit_command([&](VkCommandBuffer command) {
			VkBufferImageCopy copy = {};
			copy.bufferOffset = 0;
			copy.bufferRowLength = 0;
			copy.bufferImageHeight = 0;
			copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copy.imageSubresource.mipLevel = 0;
			copy.imageSubresource.baseArrayLayer = 0;
			copy.imageSubresource.layerCount = 1;
			copy.imageExtent = VkExtent3D{width, height, 1};

			device().cmdCopyBufferToImage(command, staging_buffer, image->image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
		});

		vmaDestroyBuffer(vkctx.allocator, staging_buffer, staging_allocation);
	}

	static AllocatedImage create_image(
		uint32_t width,
		uint32_t height,
		VkFormat format,
		VkImageUsageFlags usage
	) {
		VkImageCreateInfo info = info::image_create_info(width, height, 1);
		info.imageType = VK_IMAGE_TYPE_2D;
		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.format = format;
		info.tiling = VK_IMAGE_TILING_OPTIMAL;

		info.usage = usage;

		auto allocation_create_info = info::allocation_create_info(0);
		allocation_create_info.usage = VMA_MEMORY_USAGE_AUTO;

		VkImage image;
		VmaAllocation allocation;
		VmaAllocationInfo allocation_info;

		VkImageViewCreateInfo image_view_create = {};

		VK_CHECK(vmaCreateImage(
			vkctx.allocator,
			&info,
			&allocation_create_info,
			&image,
			&allocation,
			&allocation_info
		));

		VkImageView view;
		image_view_create.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		image_view_create.viewType = VK_IMAGE_VIEW_TYPE_2D;
		image_view_create.format = format;
		image_view_create.subresourceRange.baseMipLevel = 0;
		image_view_create.subresourceRange.baseArrayLayer = 0;
		image_view_create.subresourceRange.layerCount = 1;
		image_view_create.subresourceRange.levelCount = 1;
		image_view_create.subresourceRange.aspectMask = format == VK_FORMAT_D32_SFLOAT ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		image_view_create.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		image_view_create.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		image_view_create.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		image_view_create.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		image_view_create.image = image;

		VK_CHECK(device().createImageView(&image_view_create, nullptr, &view));

		submit_command([&](VkCommandBuffer command) {
			transition(command, image, VK_IMAGE_LAYOUT_UNDEFINED, format == VK_FORMAT_D32_SFLOAT ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL);
		});

		return AllocatedImage{
			.image = image,
			.view = view,
			.allocation = allocation
		};

	}
public:
	static Texture2D create(uint32_t width, uint32_t height, VkFormat format, std::function<uint32_t(int x, int y)> &&fn) {
		std::vector<uint32_t> array(width * height);
		std::generate(array.begin(), array.end(),
			[&, x = 0, y = 0]() mutable {
				uint32_t value = fn(x, y);
				if (++x == width) { x = 0; ++y; }
				return value;
		});
		return Texture2D::create(width, height, format, array.data());
	}

	static Texture2D create(uint32_t width, uint32_t height, AllocatedImage image) {
		VkDescriptorImageInfo desc_image = {};
		desc_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		desc_image.imageView = image.view;
		desc_image.sampler = vkctx.sampler_address_repeat;

		VkWriteDescriptorSet write_desc = {};
		write_desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write_desc.dstSet = bindless_texture_desc;
		write_desc.dstBinding = 0;
		write_desc.dstArrayElement = bindless_texture_index;
		write_desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write_desc.descriptorCount = 1;
		write_desc.pImageInfo = &desc_image;

		device().updateDescriptorSets(1, &write_desc, 0, nullptr);

		return Texture2D(M{
			.image = image,
			.handle = bindless_texture_index++,
			.width = width,
			.height = height
		});
	}

	static Texture2D create_empty(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlagBits usage) {
		auto image_res = create_image(
			width,
			height,
			format,
			usage | VK_IMAGE_USAGE_SAMPLED_BIT
		);

		VkDescriptorImageInfo desc_image = {};
		desc_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		desc_image.imageView = image_res.view;
		desc_image.sampler = vkctx.sampler_address_repeat;

		VkWriteDescriptorSet write_desc = {};
		write_desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write_desc.dstSet = bindless_texture_desc;
		write_desc.dstBinding = 0;
		write_desc.dstArrayElement = bindless_texture_index;
		write_desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write_desc.descriptorCount = 1;
		write_desc.pImageInfo = &desc_image;

		device().updateDescriptorSets(1, &write_desc, 0, nullptr);

		return Texture2D(M{
			.image = image_res,
			.handle = bindless_texture_index++,
			.width = width,
			.height = height
		});
	}

	static Texture2D create(uint32_t width, uint32_t height, VkFormat format, void *data) {
		auto image_res = create_image(
			width,
			height,
			format,
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
		);
		transfer_data(width, height, &image_res, data, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VkDescriptorImageInfo desc_image = {};
		desc_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		desc_image.imageView = image_res.view;
		desc_image.sampler = vkctx.sampler_address_repeat;

		VkWriteDescriptorSet write_desc = {};
		write_desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write_desc.dstSet = bindless_texture_desc;
		write_desc.dstBinding = 0;
		write_desc.dstArrayElement = bindless_texture_index;
		write_desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write_desc.descriptorCount = 1;
		write_desc.pImageInfo = &desc_image;

		device().updateDescriptorSets(1, &write_desc, 0, nullptr);

		return Texture2D(M{
			.image = image_res,
			.handle = bindless_texture_index++,
			.width = width,
			.height = height
		});
	}

	uint32_t handle() {
		return m.handle;
	}

	AllocatedImage image() {
		return m.image;
	}
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
		write_desc.dstSet = bindless_storage_desc;
		write_desc.dstBinding = 0;
		write_desc.dstArrayElement = bindless_storage_index;
		write_desc.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		write_desc.descriptorCount = 1;
		write_desc.pBufferInfo = &buffer_info_desc;

		device().updateDescriptorSets(1, &write_desc, 0, nullptr);

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
		VmaAllocation allocation;
		VkBuffer buffer;
		size_t count;
		uint32_t handle;
		T *wrap;
	} m;

	explicit SharedBuffer<T>(M m) : m(std::move(m)) {}
public:
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
		write_desc.dstSet = bindless_storage_desc;
		write_desc.dstBinding = 0;
		write_desc.dstArrayElement = bindless_storage_index;
		write_desc.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		write_desc.descriptorCount = 1;
		write_desc.pBufferInfo = &buffer_info_desc;

		device().updateDescriptorSets(1, &write_desc, 0, nullptr);

		return SharedBuffer<T>(M {
			.allocation = allocation,
			.buffer = buffer,
			.count = count,
			.handle = bindless_storage_index++,
			.wrap = (T*)mapped_data,
		});
	}

	uint32_t handle() {
		return m.handle;
	}

	T *operator->() {
		return m.wrap;
	}

	T &operator[](size_t index) {
		return m.wrap[index];
	}
};

class FrameGraph {
public:
	struct Input {
		std::string name;
	};

	struct Output {
		std::string name;
		VkAttachmentLoadOp load_op;
		VkAttachmentStoreOp store_op;
		VkClearValue clear_value;
	};
private:
	struct Resource {
		std::string name;
		AllocatedImage image;
		VkImageLayout layout;
		VkFormat format;
		VkExtent2D extent = {0,0};
	};

	struct Pass {
		std::string name;

		using ExecuteFn = std::function<void(VkCommandBuffer)>;
		ExecuteFn execute;

		std::vector<Input> inputs;
		std::vector<Output> outputs;
		bool disabled;
	};

	struct M {
		std::vector<Pass> passes;
		std::unordered_map<std::string, Resource> resources;
	} m;

	explicit FrameGraph(M m) : m(std::move(m)) {}
public:
	static FrameGraph create() {
		return FrameGraph(M {});
	}

	FrameGraph &add_resource(const std::string name, AllocatedImage image, VkFormat format, VkExtent2D extent) {
		m.resources[name] = {
			name,
			image,
			VK_IMAGE_LAYOUT_UNDEFINED,
			format,
			extent
		};

		return *this;
	}

	FrameGraph &add_resource(const std::string name, VkFormat format, VkExtent2D extent) {
		m.resources[name] = {
			name,
			AllocatedImage{},
			VK_IMAGE_LAYOUT_UNDEFINED,
			format,
			extent
		};

		return *this;
	}

	FrameGraph &update_resource(const std::string name, AllocatedImage image, VkFormat format, VkExtent2D extent) {
		m.resources[name].image = image;
		m.resources[name].format = format;
		m.resources[name].extent = extent;

		return *this;
	}

	FrameGraph &add_pass(const std::string name, std::vector<Input> inputs, std::vector<Output> outputs, Pass::ExecuteFn execute) {
		m.passes.push_back({name, execute, inputs, outputs});
		return *this;
	}

	FrameGraph &compile() {
		for(const auto &node : m.passes) {
			for (const auto &input : node.inputs) {
				if (m.resources.find(input.name) == m.resources.end()) {
					throw std::runtime_error("Input resource " + input.name + " not found for pass " + node.name);
				}
			}

			for (const auto &output : node.outputs) {
				if (m.resources.find(output.name) == m.resources.end()) {
					throw std::runtime_error("Output resource " + output.name + " not found for pass " + node.name);
				}
			}
		}

		return *this;
	}

	void execute(VkCommandBuffer cmd) {
		struct Transition {
			VkImageMemoryBarrier2 barrier;
			Resource *resource;
		};

		std::vector<Transition> pending_transitions;

		for(auto &pass : m.passes) {
			for (const auto& input : pass.inputs) {
				Resource& resource = m.resources[input.name];
				if (resource.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
					pending_transitions.push_back({
						{
							.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
							.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
							.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
							.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
							.dstAccessMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
							.oldLayout = resource.layout,
							.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
							.image = resource.image.image,
							.subresourceRange = info::image_subresource_range(
								resource.layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL ?
									VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT
							)
						},
						&resource
					});
				}
			}

			for (const auto& output : pass.outputs) {
				Resource& resource = m.resources[output.name];
				const bool is_depth = resource.format == VK_FORMAT_D32_SFLOAT;
				const VkImageLayout target_layout = is_depth ?
					VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL :
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

				if (resource.layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL || !is_depth) {
					pending_transitions.push_back({
						{
							.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
							.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
							.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
							.dstStageMask = is_depth ?
								VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT :
								VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
							.dstAccessMask = is_depth ?
								VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT :
								VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
							.oldLayout = resource.layout,
							.newLayout = is_depth ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
							.image = resource.image.image,
							.subresourceRange = info::image_subresource_range(
								is_depth ?
									VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT
							)
						},
						&resource
					});
				}
			}

			if(!pending_transitions.empty()) {
				std::vector<VkImageMemoryBarrier2> barriers;
				for(auto &transition : pending_transitions) {
					barriers.push_back(transition.barrier);
					transition.resource->layout = transition.barrier.newLayout;
				}

				VkDependencyInfo dependency_info = {};
				dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
				dependency_info.imageMemoryBarrierCount = barriers.size();
				dependency_info.pImageMemoryBarriers = barriers.data();

				device().cmdPipelineBarrier2(cmd, &dependency_info);

				pending_transitions.clear();
			}

			if(!pass.outputs.empty()) {
				VkRenderingInfoKHR rendering_info = {
					VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
					nullptr,
					0,
					{0, 0, m.resources[pass.outputs[0].name].extent.width, m.resources[pass.outputs[0].name].extent.height},
					1,
					0,
					0,
					nullptr,
					nullptr,
					nullptr
				};

				std::vector<VkRenderingAttachmentInfoKHR> color_attachments;
				std::pair<bool, VkRenderingAttachmentInfoKHR> depth;
				for (const auto& output : pass.outputs) {
					Resource& resource = m.resources[output.name];

					if(resource.format == VK_FORMAT_D32_SFLOAT) {
						depth.second = {
							VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
							nullptr,
							resource.image.view,
							resource.layout,
							VK_RESOLVE_MODE_NONE,
							VK_NULL_HANDLE,
							resource.layout,
							output.load_op,
							output.store_op,
							output.clear_value
						};
						depth.first = true;
						continue;
					}

					color_attachments.push_back({
						VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
						nullptr,
						resource.image.view,
						resource.layout,
						VK_RESOLVE_MODE_NONE,
						VK_NULL_HANDLE,
						resource.layout,
						output.load_op,
						output.store_op,
						output.clear_value
					});
				}

				rendering_info.colorAttachmentCount = static_cast<uint32_t>(color_attachments.size());
				rendering_info.pColorAttachments = color_attachments.data();
				rendering_info.pDepthAttachment = depth.first ? &depth.second : nullptr;

				VkDebugUtilsLabelEXT label = {};
				label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
				label.pLabelName = pass.name.c_str();
				label.color[0] = 0.0;
				label.color[1] = 0.0;
				label.color[2] = 0.0;
				label.color[3] = 1.0;

				device().cmdBeginDebugUtilsLabelEXT(cmd, &label);
				device().cmdBeginRendering(cmd, &rendering_info);
			}

			pass.execute(cmd);

			if(!pass.outputs.empty()) {
				device().cmdEndRendering(cmd);
				device().cmdEndDebugUtilsLabelEXT(cmd);
			}
		}
	}
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

struct Vertex {
	glm::vec4 position;
	glm::vec4 texcoord;
	glm::vec4 normal;
};

struct SceneData {
	glm::vec4 camera_position;
	glm::mat4 projection;
	glm::mat4 view;
};

struct ObjectData {
	glm::mat4 model;
};

struct LightData {
	glm::vec4 position;
	glm::vec4 color;
};

struct MaterialData {
	uint32_t albedo_handle;
	uint32_t shadow_handle;
	float metallic;
	float roughness;
	float ao;
	VkPipeline pipeline;
};

static std::vector<Vertex> CUBE_VERTEX = {
		{{-1.0f, -1.0f,  1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}}, // Bottom-left
		{{ 1.0f, -1.0f,  1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}}, // Bottom-right
		{{ 1.0f,  1.0f,  1.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}}, // Top-right
		{{-1.0f,  1.0f,  1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}}, // Top-left

		// Face 2 (Back)
		{{-1.0f, -1.0f, -1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f, 0.0f}}, // Bottom-left (uv reversed for texture orientation)
		{{ 1.0f, -1.0f, -1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f, 0.0f}}, // Bottom-right
		{{ 1.0f,  1.0f, -1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f, 0.0f}}, // Top-right
		{{-1.0f,  1.0f, -1.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f, 0.0f}}, // Top-left

		// Face 3 (Top)
		{{-1.0f,  1.0f, -1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}}, // Bottom-left (relative to top face)
		{{ 1.0f,  1.0f, -1.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}}, // Bottom-right
		{{ 1.0f,  1.0f,  1.0f, 1.0f}, {1.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}}, // Top-right
		{{-1.0f,  1.0f,  1.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}}, // Top-left

		// Face 4 (Bottom)
		{{-1.0f, -1.0f, -1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f, 0.0f}}, // Bottom-left
		{{ 1.0f, -1.0f, -1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f, 0.0f}}, // Bottom-right
		{{ 1.0f, -1.0f,  1.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f, 0.0f}}, // Top-right
		{{-1.0f, -1.0f,  1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f, 0.0f}}, // Top-left

		// Face 5 (Right)
		{{ 1.0f, -1.0f, -1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}}, // Bottom-left
		{{ 1.0f, -1.0f,  1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}}, // Bottom-right
		{{ 1.0f,  1.0f,  1.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}}, // Top-right
		{{ 1.0f,  1.0f, -1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}}, // Top-left

		// Face 6 (Left)
		{{-1.0f, -1.0f, -1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}}, // Bottom-left
		{{-1.0f, -1.0f,  1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}}, // Bottom-right
		{{-1.0f,  1.0f,  1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}}, // Top-right
		{{-1.0f,  1.0f, -1.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}}  // Top-left
};

static std::vector<uint32_t> CUBE_INDEX = {
		// Face 1 (Front)
		0, 1, 2,
		2, 3, 0,

		// Face 2 (Back)
		4, 5, 6,
		6, 7, 4,

		// Face 3 (Top)
		8, 9, 10,
		10, 11, 8,

		// Face 4 (Bottom)
		12, 13, 14,
		14, 15, 12,

		// Face 5 (Right)
		16, 17, 18,
		18, 19, 16,

		// Face 6 (Left)
		20, 21, 22,
		22, 23, 20
};


void build_pipelines() {
	for(auto &pipeline : pipelines) {
		if(pipeline.second != VK_NULL_HANDLE) {
			device().deviceWaitIdle();
			device().destroyPipeline(pipeline.second, nullptr);
		}
	}

	auto bindless = SlangProgram::create(
			"bindless",
			"C:\\Users\\rwf93\\Desktop\\bindless_test\\bindless\\src\\bindless.slang"
	);

	auto bindless_pbr = SlangProgram::create(
		"bindless_pbr",
		"C:\\Users\\rwf93\\Desktop\\bindless_test\\bindless\\src\\bindless_pbr.slang"
	);

	auto shadow = SlangProgram::create(
		"shadow",
		"C:\\Users\\rwf93\\Desktop\\bindless_test\\bindless\\src\\shadow.slang"
	);

	auto swapchain_write = SlangProgram::create(
		"swapchain_write",
		"C:\\Users\\rwf93\\Desktop\\bindless_test\\bindless\\src\\swapchain_write.slang"
	);

	create_pipeline(
		"bindless_framegraph",
		bindless,
		{ VK_FORMAT_R16G16B16A16_UNORM, VK_FORMAT_R8G8B8A8_UNORM }, VK_FORMAT_D32_SFLOAT
	);

	create_pipeline(
		"bindless_framegraph_pbr",
		bindless_pbr,
		{ VK_FORMAT_R16G16B16A16_UNORM, VK_FORMAT_R8G8B8A8_UNORM }, VK_FORMAT_D32_SFLOAT
	);

	create_pipeline(
		"shadow",
		shadow,
		{}, VK_FORMAT_D32_SFLOAT
	);

	create_pipeline(
		"swapchain_write",
		swapchain_write,
		{ VK_FORMAT_B8G8R8A8_UNORM }
	);
}

int main(int argc, char** argv) {
	if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
		spdlog::error("Couldn't initalize SDL");
	}

	window = SDL_CreateWindow("fuck", 1600, 900, SDL_WINDOW_VULKAN);
	SDL_ShowWindow(window);

	create_vk_shit();
	build_pipelines();
	create_imgui_shit();

	auto triangle_vbo = GPUBuffer<Vertex>::create(CUBE_VERTEX);
	auto triangle_ibo = GPUBuffer<uint32_t>::create(CUBE_INDEX);

	auto scene = SharedBuffer<SceneData>::create(1);
	auto objects = SharedBuffer<ObjectData>::create(1024);
	auto materials = SharedBuffer<MaterialData>::create(512);
	auto lights = SharedBuffer<LightData>::create(12);

	uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 0));
	uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));

	auto missing_texture = Texture2D::create(16, 16, VK_FORMAT_R8G8B8A8_UNORM,
		[=](int x, int y) { return ((x % 2) ^ (y % 2)) ? magenta : black; }
	);

	auto camera = Camera::create();

	auto color = Texture2D::create_empty(
		1600, 900,
		VK_FORMAT_R16G16B16A16_UNORM,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
	);

	auto normal = Texture2D::create_empty(
		1600, 900,
		VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
	);

	auto depth = Texture2D::create_empty(
		1600,
		900,
		VK_FORMAT_D32_SFLOAT,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
	);

	auto shadowmap = Texture2D::create_empty(
		1024,
		1024,
		VK_FORMAT_D32_SFLOAT,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
	);

	objects[0].model = calculate_model_matrix(glm::vec3(0), glm::vec3(0), glm::vec3(1));
	objects[1].model = calculate_model_matrix(glm::vec3(1.5, -3, 0), glm::vec3(0), glm::vec3(1));

	materials[0].albedo_handle = missing_texture.handle();
	materials[0].shadow_handle = shadowmap.handle();
	materials[0].metallic = 0.5;
	materials[0].roughness = 0.22;
	materials[0].ao = 1.0f;
	materials[0].pipeline = pipelines["bindless_framegraph_pbr"];

	lights[0].position = glm::vec4(-10.0f, 10.0f, -10.0f, 10.0f);
	lights[0].color = glm::vec4(255, 255, 255, 255);

	auto draw_scene = [&](VkCommandBuffer cmd) -> void {
		device().cmdDraw(cmd, triangle_ibo.count(), 2, 0, 0);
	};

	auto normal_imgui = ImGui_ImplVulkan_AddTexture(vkctx.sampler_address_repeat, color.image().view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	auto framegraph = FrameGraph::create()
		.add_resource("color", color.image(), VK_FORMAT_R16G16B16A16_UNORM, VkExtent2D{1600, 900})
		.add_resource("normal", normal.image(), VK_FORMAT_R8G8B8A8_UNORM, VkExtent2D{1600, 900})
		.add_resource("depth", depth.image(), VK_FORMAT_D32_SFLOAT, VkExtent2D{1600, 900})
		.add_resource("shadowmap", shadowmap.image(), VK_FORMAT_D32_SFLOAT, VkExtent2D{1024, 1024})
		.add_resource("swapchain", VK_FORMAT_B8G8R8A8_UNORM, VkExtent2D{1600, 900})
		.add_pass("shadow",
			{},
			{
				FrameGraph::Output{
					.name = "shadowmap",
					.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
					.store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE,
					.clear_value = {1.0, 1.0, 1.0, 1.0}
				}
			},
			[&](VkCommandBuffer cmd) {
				float near_plane = 1.0f, far_plane = 7.5f;
				/*
				PushConstants push_constants 	= {};
				push_constants.vbo_handle 		= triangle_vbo.handle();
				push_constants.ibo_handle 		= triangle_ibo.handle();
				push_constants.scene_handle 	= scene.handle();
				push_constants.object_handle 	= objects.handle();
				push_constants.light_handle 	= lights.handle();
				push_constants.material_handle  = materials.handle();
				push_constants.material 		= 0;

				device().cmdPushConstants(
					cmd,
					global_layout,
					VK_SHADER_STAGE_ALL,
					0,
					sizeof(PushConstants),
					&push_constants
				);
				device().cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines["shadow"]);
				draw_scene(cmd);
				*/
			}
		)
		.add_pass("gbuffer",
			{
				FrameGraph::Input{
					.name = "shadowmap"
				},
			},
			{
				FrameGraph::Output{
					.name = "color",
					.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
					.store_op = VK_ATTACHMENT_STORE_OP_STORE,
					.clear_value = {0.0, 0.0, 0.0, 1.0}
				},
				FrameGraph::Output{
					.name = "normal",
					.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
					.store_op = VK_ATTACHMENT_STORE_OP_STORE,
					.clear_value = {0.0, 0.0, 0.0, 1.0}
				},
				FrameGraph::Output{
					.name = "depth",
					.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
					.store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE,
					.clear_value = {1.0, 1.0, 1.0, 1.0}
				}
			},
			[&](VkCommandBuffer cmd){
				scene->projection = glm::perspective(
					glm::radians(70.0f),
					1600.0f / 900.0f,
					0.1f,
					100.0f
				);
				scene->projection[1][1] *= -1;

				scene->view = camera.get_view_matrix();
				scene->camera_position = glm::vec4(camera.get_position(), 0);

				PushConstants push_constants 	= {};
				push_constants.vbo_handle 		= triangle_vbo.handle();
				push_constants.ibo_handle 		= triangle_ibo.handle();
				push_constants.scene_handle 	= scene.handle();
				push_constants.object_handle 	= objects.handle();
				push_constants.light_handle 	= lights.handle();
				push_constants.material_handle  = materials.handle();
				push_constants.material 		= 0;

				device().cmdPushConstants(
					cmd,
					global_layout,
					VK_SHADER_STAGE_ALL,
					0,
					sizeof(PushConstants),
					&push_constants
				);
				device().cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines["bindless_framegraph_pbr"]);
				draw_scene(cmd);
			}
		)
		.add_pass("swapchain_write",
			{
				FrameGraph::Input{
					.name = "color"
				},
				FrameGraph::Input{
					.name = "normal"
				},
				FrameGraph::Input{
					.name = "depth"
				}
			},
			{
				FrameGraph::Output{
					.name = "swapchain",
					.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
					.store_op = VK_ATTACHMENT_STORE_OP_STORE,
					.clear_value = {0.0, 0.0, 0.0, 1.0}
				}
			},
			[&](VkCommandBuffer cmd){
				PushConstants push_constants = {};
				push_constants.vbo_handle 						= UINT32_MAX;
				push_constants.ibo_handle						= UINT32_MAX;
				push_constants.scene_handle 					= UINT32_MAX;
				push_constants.object_handle 					= UINT32_MAX;
				push_constants.swapchain_write_texture_handle 	= color.handle();
				device().cmdPushConstants(
					cmd,
					global_layout,
					VK_SHADER_STAGE_ALL,
					0,
					sizeof(PushConstants),
					&push_constants
				);

				device().cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines["swapchain_write"]);
				device().cmdDraw(cmd, 3, 1, 0, 0);
			}
		)
		.add_pass("imgui_write",
			{},
			{
				FrameGraph::Output{
					.name = "swapchain",
					.load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
					.store_op = VK_ATTACHMENT_STORE_OP_STORE,
					.clear_value = {0.0, 0.0, 0.0, 0.0}
				}
			},
			[&](VkCommandBuffer cmd){
				ImGui_ImplVulkan_NewFrame();
				ImGui_ImplSDL3_NewFrame();
				ImGui::NewFrame();

				ImGui::ShowDemoWindow();

				ImGui::Render();

				ImDrawData* draw_data = ImGui::GetDrawData();
				ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);
			}
		)
		.compile();

	while(!quit) {
		while(SDL_PollEvent(&event)) {
			if(event.type == SDL_EVENT_QUIT)
				quit = true;
			camera.process_event(&event, window);
			ImGui_ImplSDL3_ProcessEvent(&event);

			if(event.type == SDL_EVENT_KEY_DOWN)
				if(event.key.key == SDLK_F4)
					build_pipelines();
		}
		camera.update();

		VK_CHECK(device().waitForFences(1, &vkctx.frames[frame_index].fence, VK_TRUE, UINT64_MAX));
		VK_CHECK(device().resetFences(1, &vkctx.frames[frame_index].fence));
		VK_CHECK(device().resetCommandBuffer(vkctx.frames[frame_index].buffer, 0));

		static VkCommandBufferBeginInfo begin_info = {};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		device().acquireNextImageKHR(
			vkctx.swapchain,
			UINT64_MAX,
			vkctx.frames[frame_index].acquire,
			VK_NULL_HANDLE,
			&image_index
		);

		device().beginCommandBuffer(vkctx.frames[frame_index].buffer, &begin_info);

		VkRect2D scissor = { {0, 0}, {1600, 900} };
		VkViewport viewport = {0, 0, 1600, 900, 0, 1};
		device().cmdSetScissor(
			vkctx.frames[frame_index].buffer,
			0,
			1,
			&scissor
		);

		device().cmdSetViewport(
			vkctx.frames[frame_index].buffer,
			0,
			1,
			&viewport
		);

		static std::vector<VkDescriptorSet> bound_sets = {
			bindless_storage_desc,
			bindless_texture_desc
		};

		device().cmdBindDescriptorSets(
			vkctx.frames[frame_index].buffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			global_layout,
			0,
			bound_sets.size(),
			bound_sets.data(),
			0,
			nullptr
		);

		transition(vkctx.swapchain_images[image_index], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		framegraph
			.update_resource(
				"swapchain",
				{vkctx.swapchain_images[frame_index], vkctx.swapchain_views[frame_index]},
				VK_FORMAT_B8G8R8A8_UNORM,
				VkExtent2D{1600, 900}
			)
			.execute(vkctx.frames[frame_index].buffer);

		transition(vkctx.swapchain_images[image_index], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
		device().endCommandBuffer(vkctx.frames[frame_index].buffer);

		auto command_info = info::command_buffer_submit_info(vkctx.frames[frame_index].buffer);
		auto wait_info = info::semaphore_submit_info(
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
			vkctx.frames[frame_index].acquire
		);
		auto signal_info = info::semaphore_submit_info(
			VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
			vkctx.frames[image_index].submit
		);

		auto submit_info = info::submit_info(&command_info, &signal_info, &wait_info);
		VK_CHECK(device().queueSubmit2(vkctx.device.get_queue(vkb::QueueType::graphics).value(), 1, &submit_info, vkctx.frames[frame_index].fence));

		VkPresentInfoKHR present_info = {};
		present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present_info.pSwapchains = &vkctx.swapchain.swapchain;
		present_info.swapchainCount = 1;
		present_info.pWaitSemaphores = &vkctx.frames[frame_index].submit;
		present_info.waitSemaphoreCount = 1;
		present_info.pImageIndices = &image_index;

		VK_CHECK(device().queuePresentKHR(vkctx.device.get_queue(vkb::QueueType::graphics).value(), &present_info));

		frame_index = (frame_index + 1) % vkctx.max_frames;
	}

	return 0;
}
