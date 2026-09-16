#include "shader.h"

#include <cstring>
#include <utility>

#include <spdlog/spdlog.h>

MaterialLayout MaterialLayout::reflect(
	SlangProgram &program,
	const char *struct_name
) {
	MaterialLayout layout;
	auto module = program.module();
	if(!module)
		return layout;

	slang::DeclReflection *module_decl = module->getModuleReflection();
	if(!module_decl)
		return layout;

	slang::ProgramLayout *program_layout = program.component()->getLayout(0);
	if(!program_layout)
		return layout;

	for(auto child : module_decl->getChildren()) {
		if(child->getKind() != slang::DeclReflection::Kind::Struct)
			continue;

		const char *name = child->getName();
		if(!name || std::strcmp(name, struct_name) != 0)
			continue;

		slang::TypeReflection *type = child->getType();
		if(!type)
			return layout;

		slang::TypeLayoutReflection *type_layout =
			program_layout->getTypeLayout(type);
		if(!type_layout)
			return layout;

		layout.stride = type_layout->getStride();
		for(unsigned int i = 0; i < type_layout->getFieldCount(); i++) {
			auto *field = type_layout->getFieldByIndex(i);
			if(!field || !field->getName())
				continue;

			MaterialField reflected_field;
			reflected_field.name = field->getName();
			reflected_field.offset = uint32_t(field->getOffset());
			reflected_field.size = field->getTypeLayout()
				? field->getTypeLayout()->getStride()
				: 0;

			layout.by_name[reflected_field.name] = layout.fields.size();
			layout.fields.push_back(std::move(reflected_field));
		}

		spdlog::debug(
			"reflected Material layout: stride={} fields={}",
			layout.stride,
			layout.fields.size()
		);
		for(const auto &field : layout.fields) {
			spdlog::debug(
				"\tMaterial field '{}' @ offset {} ({} bytes)",
				field.name,
				field.offset,
				field.size
			);
		}

		return layout;
	}

	return layout; // Shader declares no Material struct.
}
