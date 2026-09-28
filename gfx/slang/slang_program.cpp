#include "gfx/slang/shader.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include <spdlog/spdlog.h>

namespace gfx {

SlangProgram SlangProgram::create(const char *name, std::string path) {
	const std::string shader_path = std::move(path);
	return create_impl(
		name,
		shader_path,
		[shader_path] {
			std::ifstream source_file(
				std::filesystem::path(shader_path),
				std::ios::binary
			);
			if(!source_file) {
				throw std::runtime_error(
					"Failed to open Slang shader: " + shader_path
				);
			}

			return std::string{
				std::istreambuf_iterator<char>(source_file),
				std::istreambuf_iterator<char>()
			};
		},
		true
	);
}

SlangProgram SlangProgram::create_from_source(
	const char *name,
	std::string source,
	std::string virtual_path
) {
	if(virtual_path.empty())
		virtual_path = name && *name ? name : "<memory>";

	return create_impl(
		name,
		std::move(virtual_path),
		[source = std::move(source)] { return source; },
		false
	);
}

SlangProgram SlangProgram::create_impl(
	const char *name,
	std::string virtual_path,
	std::function<std::string()> load_source,
	bool retry_on_failure
) {
	if(name == nullptr || *name == '\0')
		throw std::invalid_argument("SlangProgram: name must not be empty");
	if(!load_source)
		throw std::invalid_argument("SlangProgram: source loader must not be empty");

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
		const std::string source = load_source();
		return Slang::ComPtr<slang::IModule>{
			session->loadModuleFromSourceString(
				name,
				virtual_path.c_str(),
				source.c_str(),
				diagnostics.writeRef()
			)
		};
	};
	Slang::ComPtr<slang::IModule> module = load_module();

	// A diagnostics blob can contain warnings even when compilation succeeded.
	// Only retry when Slang failed to produce a module; otherwise native
	// DescriptorHandle capability warnings would cause an infinite compile loop.
	while(!module.get() && retry_on_failure) {
		if(diagnostics.get())
			spdlog::error("{}", (char*)diagnostics->getBufferPointer());
		else
			spdlog::error(
				"Slang failed to compile '{}' without diagnostics",
				virtual_path
			);

		module.setNull();
		session.setNull();
		diagnostics.setNull();

		M::global_session->createSession(session_desc, session.writeRef());
		module = load_module();
	}
	if(!module.get()) {
		std::string message = "Failed to compile Slang shader: " + virtual_path;
		if(diagnostics.get()) {
			message += "\n";
			message.append(
				static_cast<const char *>(diagnostics->getBufferPointer()),
				diagnostics->getBufferSize()
			);
		}
		throw std::runtime_error(std::move(message));
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

slang::IModule *SlangProgram::module() const {
	return m.module.get();
}

slang::IComponentType *SlangProgram::component() const {
	return m.component.get();
}

Slang::ComPtr<ISlangBlob> SlangProgram::spirv() const {
	Slang::ComPtr<ISlangBlob> spirv;
	m.component->getTargetCode(0, spirv.writeRef());
	return spirv;
}

} // namespace gfx
