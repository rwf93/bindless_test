#include "shader.h"

#include <stdexcept>
#include <vector>

#include "vktools.h"

Pipeline Pipeline::create(
	SlangProgram &program,
	std::vector<VkFormat> color_attachments,
	VkFormat depth_format,
	VkCullModeFlagBits cullmode,
	bool depth_test,
	bool depth_write,
	VkCompareOp depth_compare
) {
	auto layout = program.component()->getLayout();

	VkPipelineInputAssemblyStateCreateInfo assembly_info = {};
	VkPipelineViewportStateCreateInfo viewport_info = {};
	VkPipelineRasterizationStateCreateInfo raster_info = {};
	VkPipelineMultisampleStateCreateInfo multisampling_info = {};
	VkPipelineColorBlendStateCreateInfo color_info = {};
	VkPipelineDepthStencilStateCreateInfo stencil_info = {};
	VkPipelineDynamicStateCreateInfo dynamic_info = {};
	VkPipelineVertexInputStateCreateInfo input_info = {};
	VkGraphicsPipelineCreateInfo pipeline_info = {};

	assembly_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	viewport_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	raster_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	multisampling_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	color_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	stencil_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	dynamic_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

	assembly_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	assembly_info.primitiveRestartEnable = VK_FALSE;

	viewport_info.viewportCount = 1;
	viewport_info.scissorCount = 1;

	raster_info.polygonMode = VK_POLYGON_MODE_FILL;
	raster_info.lineWidth = 1.0f;
	raster_info.cullMode = cullmode;
	raster_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

	multisampling_info.sampleShadingEnable = VK_FALSE;
	multisampling_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisampling_info.alphaToOneEnable = VK_FALSE;
	multisampling_info.alphaToCoverageEnable = VK_FALSE;

	color_info.logicOpEnable = VK_FALSE;
	color_info.logicOp = VK_LOGIC_OP_COPY;

	std::vector<VkPipelineColorBlendAttachmentState> blend_attachments(
		color_attachments.size()
	);
	for(auto &blend_attachment : blend_attachments) {
		blend_attachment = {};
		blend_attachment.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT |
			VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT |
			VK_COLOR_COMPONENT_A_BIT;
		blend_attachment.blendEnable = VK_FALSE;
	}
	color_info.attachmentCount = uint32_t(blend_attachments.size());
	color_info.pAttachments = blend_attachments.data();

	stencil_info.depthTestEnable = depth_test ? VK_TRUE : VK_FALSE;
	stencil_info.depthWriteEnable = depth_write ? VK_TRUE : VK_FALSE;
	stencil_info.depthCompareOp = depth_compare;
	stencil_info.depthBoundsTestEnable = VK_FALSE;
	stencil_info.stencilTestEnable = VK_FALSE;
	stencil_info.front = {};
	stencil_info.back = {};
	stencil_info.minDepthBounds = 0.0f;
	stencil_info.maxDepthBounds = 1.0f;

	std::vector<VkDynamicState> dynamic_states = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};
	dynamic_info.pDynamicStates = dynamic_states.data();
	dynamic_info.dynamicStateCount = uint32_t(dynamic_states.size());

	auto rendering_create_info = info::rendering_create_info(
		color_attachments,
		depth_format
	);

	auto code = program.blob(0);
	VkShaderModuleCreateInfo shader_module_info = {};
	shader_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	shader_module_info.codeSize = code->getBufferSize();
	shader_module_info.pCode =
		reinterpret_cast<const uint32_t*>(code->getBufferPointer());

	VkShaderModule shader_module;
	VK_CHECK(device().createShaderModule(
		&shader_module_info,
		nullptr,
		&shader_module
	));

	std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
	for(uint32_t i = 0; i < layout->getEntryPointCount(); i++) {
		auto entry = layout->getEntryPointByIndex(i);

		VkPipelineShaderStageCreateInfo stage = {};
		stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage.module = shader_module;
		stage.pName = entry->getName();

		switch(entry->getStage()) {
		case SLANG_STAGE_VERTEX:
			stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
			break;
		case SLANG_STAGE_FRAGMENT:
			stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			break;
		case SLANG_STAGE_GEOMETRY:
			stage.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
			break;
		default:
			continue;
		}

		shader_stages.push_back(stage);
	}

	std::vector<VkVertexInputBindingDescription> bindings;
	std::vector<VkVertexInputAttributeDescription> attributes;
	input_info = info::input_vertex_info(bindings, attributes);

	pipeline_info.pNext = &rendering_create_info;
	pipeline_info.pStages = shader_stages.data();
	pipeline_info.stageCount = uint32_t(shader_stages.size());
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
	VK_CHECK(device().createGraphicsPipelines(
		VK_NULL_HANDLE,
		1,
		&pipeline_info,
		nullptr,
		&pipeline
	));
	device().destroyShaderModule(shader_module, nullptr);

	return Pipeline(M{
		.pipeline = pipeline,
		.material_layout = MaterialLayout::reflect(program)
	});
}

Pipeline Pipeline::create_compute(SlangProgram &program) {
	auto layout = program.component()->getLayout();
	VkPipelineShaderStageCreateInfo compute_stage = {};
	compute_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

	for(uint32_t i = 0; i < layout->getEntryPointCount(); i++) {
		auto entry = layout->getEntryPointByIndex(i);
		if(entry->getStage() != SLANG_STAGE_COMPUTE)
			continue;
		if(compute_stage.stage != 0) {
			throw std::runtime_error(
				"Pipeline::create_compute: shader defines more than one compute entry point"
			);
		}
		compute_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		compute_stage.pName = entry->getName();
	}

	if(compute_stage.stage == 0) {
		throw std::runtime_error(
			"Pipeline::create_compute: shader has no compute entry point"
		);
	}

	auto code = program.blob(0);
	VkShaderModuleCreateInfo module_info = {};
	module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module_info.codeSize = code->getBufferSize();
	module_info.pCode =
		reinterpret_cast<const uint32_t *>(code->getBufferPointer());

	VkShaderModule module;
	VK_CHECK(device().createShaderModule(&module_info, nullptr, &module));
	compute_stage.module = module;

	VkComputePipelineCreateInfo pipeline_info = {};
	pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipeline_info.stage = compute_stage;
	pipeline_info.layout = global_layout;

	VkPipeline pipeline;
	VK_CHECK(device().createComputePipelines(
		VK_NULL_HANDLE,
		1,
		&pipeline_info,
		nullptr,
		&pipeline
	));
	device().destroyShaderModule(module, nullptr);

	return Pipeline(M{
		.pipeline = pipeline,
		.material_layout = MaterialLayout::reflect(program),
	});
}
