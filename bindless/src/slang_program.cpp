#include "shader.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include <spdlog/spdlog.h>

SlangProgram SlangProgram::create(const char *name, std::string path) {
	if(!M::global_session.get())
		slang::createGlobalSession(M::global_session.writeRef());

	auto slang_targets = std::to_array<slang::TargetDesc>({
		{
			.format = SLANG_SPIRV,
			.profile = M::global_session->findProfile("spirv_1_5")
		}
	});

	auto slang_options = std::to_array<slang::CompilerOptionEntry>({
		{
			slang::CompilerOptionName::EmitSpirvDirectly,
			{slang::CompilerOptionValueKind::Int, 1},
		},
		{
			slang::CompilerOptionName::VulkanUseEntryPointName,
			{slang::CompilerOptionValueKind::Int, 1},
		},
		{
			slang::CompilerOptionName::BindlessSpaceIndex,
			{slang::CompilerOptionValueKind::Int, 0},
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
	auto load_module = [&]() {
		std::ifstream source_file(std::filesystem::path(path), std::ios::binary);
		if(!source_file)
			throw std::runtime_error("Failed to open Slang shader: " + path);

		std::string source{
			std::istreambuf_iterator<char>(source_file),
			std::istreambuf_iterator<char>()
		};
		return Slang::ComPtr<slang::IModule>{
			session->loadModuleFromSourceString(
				name,
				path.c_str(),
				source.c_str(),
				diagnostics.writeRef()
			)
		};
	};
	Slang::ComPtr<slang::IModule> module = load_module();

	// A diagnostics blob can contain warnings even when compilation succeeded.
	// Only retry when Slang failed to produce a module; otherwise native
	// DescriptorHandle capability warnings would cause an infinite compile loop.
	while(!module.get()) {
		if(diagnostics.get())
			spdlog::error("{}", (char*)diagnostics->getBufferPointer());
		else
			spdlog::error("Slang failed to compile '{}' without diagnostics", path);

		module.setNull();
		session.setNull();
		diagnostics.setNull();

		M::global_session->createSession(session_desc, session.writeRef());
		module = load_module();
	}

	if(diagnostics.get())
		spdlog::warn("{}", (char*)diagnostics->getBufferPointer());

	std::vector<slang::IComponentType*> component_types;
	component_types.push_back(module);
	for(int i = 0; i < module->getDefinedEntryPointCount(); i++) {
		slang::IEntryPoint *entry;
		module->getDefinedEntryPoint(i, &entry);
		component_types.push_back(entry);
	}

	Slang::ComPtr<slang::IComponentType> component;
	session->createCompositeComponentType(
		component_types.data(),
		component_types.size(),
		component.writeRef()
	);

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
