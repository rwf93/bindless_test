#pragma once

#include <slang.h>
#include <slang-com-ptr.h>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "gfx/device.h"
#include "gfx/pipeline.h"
#include "gfx/types.h"

namespace gfx {

class SlangProgram {
	struct M {
		static inline Slang::ComPtr<slang::IGlobalSession> global_session;
		Slang::ComPtr<slang::ISession> session;
		Slang::ComPtr<slang::IModule> module;
		Slang::ComPtr<slang::IComponentType> component;
	} m;

	explicit SlangProgram(M m) : m(std::move(m)) {};
	static SlangProgram create_impl(
		const char *name,
		std::string virtual_path,
		std::function<std::string()> load_source,
		bool retry_on_failure
	);

public:
	static SlangProgram create(const char *name, std::string path);
	static SlangProgram create_from_source(
		const char *name,
		std::string source,
		std::string virtual_path = {}
	);

	slang::IModule *module() const;
	slang::IComponentType *component() const;
	Slang::ComPtr<ISlangBlob> spirv() const;
};

struct GraphicsPipelineDesc {
	std::vector<Format> color_attachments;
	Format depth_format = Format::Undefined;
	CullMode cull_mode = CullMode::None;
	bool depth_test = true;
	bool depth_write = true;
	CompareOp depth_compare = CompareOp::Less;
};

} // namespace gfx
