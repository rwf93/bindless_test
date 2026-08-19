#pragma once

#include <vulkan/vulkan.h>
#include <slang.h>
#include <slang-com-ptr.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include "vfs.h"

#include "vkcontext.h"

class SlangProgram {
	struct M {
		static inline Slang::ComPtr<slang::IGlobalSession> global_session;
		Slang::ComPtr<slang::ISession> session;
		Slang::ComPtr<slang::IModule> module;
		Slang::ComPtr<slang::IComponentType> component;
	} m;

	explicit SlangProgram(M m) : m(std::move(m)) {};

public:
	static SlangProgram create(const char *name, std::string path);

	Slang::ComPtr<slang::IModule> module();
	Slang::ComPtr<slang::IComponentType> component();
	Slang::ComPtr<ISlangBlob> blob(uint32_t index);
};

// A single reflected field of a shader's `Material` parameter struct.
struct MaterialField {
	std::string name;
	uint32_t offset;
	size_t size;
};

// Reflected layout of a shader's `Material` struct. This is the "dynamic struct"
// on the C++ side: instead of a hard-coded `MaterialData`, we ask the shader what
// its material parameters are (name + byte offset + size) and write into a raw
// byte buffer accordingly. Stride matches the shader's `sizeof(Material)` so
// `StructuredBuffer<Material>[i]` indexes the same slot the CPU wrote.
struct MaterialLayout {
	size_t stride = 0;
	std::vector<MaterialField> fields;
	std::unordered_map<std::string, size_t> by_name;

	bool valid() const { return stride > 0; }

	const MaterialField *find(const std::string &name) const {
		auto it = by_name.find(name);
		return it == by_name.end() ? nullptr : &fields[it->second];
	}

	static MaterialLayout reflect(SlangProgram &program, const char *struct_name = "Material");
};

struct PipelineInfo {
	VkPipeline pipeline = VK_NULL_HANDLE;
	MaterialLayout material_layout;
};

extern std::map<std::string, PipelineInfo> pipelines;

void create_pipeline(
	std::string name,
	SlangProgram &program,
	std::vector<VkFormat> color_attachments,
	VkFormat depth_format = VK_FORMAT_UNDEFINED,
	VkCullModeFlagBits cullmode = VK_CULL_MODE_NONE,
	bool depth_test = true,
	bool depth_write = true
);

void build_pipelines(VFS &vfs);
