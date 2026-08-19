#include <spdlog/spdlog.h>
#include <cstring>
#include <array>

#include "shader.h"
#include "vktools.h"
#include "vfs.h"

std::map<std::string, PipelineInfo> pipelines;

SlangProgram SlangProgram::create(const char *name, std::string path) {
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
		session->loadModuleFromSource(name, path.c_str(), nullptr, diagnostics.writeRef())
	};

	// Keep recompiling the shader if a diagnostic blob exists
	while(diagnostics.get()) {
		spdlog::error("{}", (char*)diagnostics->getBufferPointer());

		module.setNull();
		session.setNull();

		M::global_session->createSession(session_desc, session.writeRef());
		module = session->loadModuleFromSource(name, path.c_str(), nullptr, diagnostics.writeRef());
	}

	Slang::ComPtr<slang::IComponentType> component;
	module->link(component.writeRef());

	return SlangProgram(M{
		.session = std::move(session),
		.module = std::move(module),
		.component = std::move(component)
	});
}

Slang::ComPtr<slang::IModule> SlangProgram::module() {
	return m.module;
}

Slang::ComPtr<slang::IComponentType> SlangProgram::component() {
	return m.component;
}

Slang::ComPtr<ISlangBlob> SlangProgram::blob(uint32_t index) {
	Slang::ComPtr<ISlangBlob> spirv;
	m.component->getTargetCode(index, spirv.writeRef());
	return spirv;
}

MaterialLayout MaterialLayout::reflect(SlangProgram &program, const char *struct_name) {
	MaterialLayout layout;
	auto module = program.module();
	if(!module) return layout;

	slang::DeclReflection *module_decl = module->getModuleReflection();
	if(!module_decl) return layout;

	slang::ProgramLayout *program_layout = program.component()->getLayout(0);
	if(!program_layout) return layout;

	for(auto child : module_decl->getChildren()) {
		if(child->getKind() != slang::DeclReflection::Kind::Struct)
			continue;

		const char *name = child->getName();
		if(!name || strcmp(name, struct_name) != 0)
			continue;

		slang::TypeReflection *type = child->getType();
		if(!type) return layout;

		slang::TypeLayoutReflection *type_layout = program_layout->getTypeLayout(type);
		if(!type_layout) return layout;

		layout.stride = type_layout->getStride();
		for(unsigned int i = 0; i < type_layout->getFieldCount(); i++) {
			auto *field = type_layout->getFieldByIndex(i);
			if(!field || !field->getName()) continue;

			MaterialField f;
			f.name = field->getName();
			f.offset = uint32_t(field->getOffset());
			f.size = field->getTypeLayout() ? field->getTypeLayout()->getStride() : 0;

			layout.by_name[f.name] = layout.fields.size();
			layout.fields.push_back(std::move(f));
		}

		spdlog::info("reflected Material layout: stride={} fields={}", layout.stride, layout.fields.size());
		for(const auto &f : layout.fields)
			spdlog::info("  Material field '{}' @ offset {} ({} bytes)", f.name, f.offset, f.size);

		return layout;
	}

	return layout; // shader declares no Material struct
}

void create_pipeline(
	std::string name,
	SlangProgram &program,
	std::vector<VkFormat> color_attachments,
	VkFormat depth_format,
	VkCullModeFlagBits cullmode,
	bool depth_test,
	bool depth_write
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
	raster_info.cullMode = cullmode;
	raster_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

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

	stencil_info.depthTestEnable = depth_test ? VK_TRUE : VK_FALSE;
	stencil_info.depthWriteEnable = depth_write ? VK_TRUE : VK_FALSE;
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

	pipelines[name] = PipelineInfo {
		.pipeline = pipeline,
		.material_layout = MaterialLayout::reflect(program)
	};
}

void build_pipelines(VFS &vfs) {
	for(auto &pipeline : pipelines) {
		if(pipeline.second.pipeline != VK_NULL_HANDLE) {
			device().deviceWaitIdle();
			device().destroyPipeline(pipeline.second.pipeline, nullptr);
		}
	}

	auto bindless = SlangProgram::create(
		"bindless",
		vfs.resolve_string("shaders/bindless.slang")
	);

	auto bindless_pbr = SlangProgram::create(
		"bindless_pbr",
		vfs.resolve_string("shaders/bindless_pbr.slang")
	);

	auto shadow = SlangProgram::create(
		"shadow",
		vfs.resolve_string("shaders/shadow.slang")
	);

	auto swapchain_write = SlangProgram::create(
		"swapchain_write",
		vfs.resolve_string("shaders/swapchain_write.slang")
	);

	auto skybox = SlangProgram::create(
		"skybox",
		vfs.resolve_string("shaders/skybox.slang")
	);

	create_pipeline(
		"bindless_framegraph",
		bindless,
		{ VK_FORMAT_R16G16B16A16_UNORM, VK_FORMAT_R8G8B8A8_UNORM }, VK_FORMAT_D32_SFLOAT
	);

	create_pipeline(
		"bindless_framegraph_pbr",
		bindless_pbr,
		{ VK_FORMAT_R16G16B16A16_UNORM, VK_FORMAT_R8G8B8A8_UNORM }, VK_FORMAT_D32_SFLOAT,
		VK_CULL_MODE_BACK_BIT
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

	create_pipeline(
		"skybox",
		skybox,
		{ VK_FORMAT_R16G16B16A16_UNORM }, VK_FORMAT_D32_SFLOAT,
		VK_CULL_MODE_NONE,
		false, false
	);
}
