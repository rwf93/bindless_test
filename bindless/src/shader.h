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

// A compiled graphics pipeline. Owns its VkPipeline handle — destructor
// destroys it, move-assign destroys the overwritten handle first (RAII,
// same shape as GPUBuffer<T>::operator=). Immutable after construction;
// `material_layout` is the reflected Material struct from the shader.
class Pipeline {
	struct M {
		VkPipeline pipeline = VK_NULL_HANDLE;
		MaterialLayout material_layout;
	} m;

	explicit Pipeline(M m) : m(std::move(m)) {}
public:
	Pipeline() = default;
	~Pipeline() {
		if(m.pipeline != VK_NULL_HANDLE)
			device().destroyPipeline(m.pipeline, nullptr);
	}

	Pipeline(const Pipeline &) = delete;
	Pipeline &operator=(const Pipeline &) = delete;
	Pipeline(Pipeline &&other) noexcept : m(std::move(other.m)) {
		other.m.pipeline = VK_NULL_HANDLE;
	}
	Pipeline &operator=(Pipeline &&other) noexcept {
		if(this != &other) {
			if(m.pipeline != VK_NULL_HANDLE)
				device().destroyPipeline(m.pipeline, nullptr);
			m = std::move(other.m);
			other.m.pipeline = VK_NULL_HANDLE;
		}
		return *this;
	}

	static Pipeline create(
		SlangProgram &program,
		std::vector<VkFormat> color_attachments,
		VkFormat depth_format = VK_FORMAT_UNDEFINED,
		VkCullModeFlagBits cullmode = VK_CULL_MODE_NONE,
		bool depth_test = true,
		bool depth_write = true
	);

	VkPipeline pipeline() const { return m.pipeline; }
	const MaterialLayout &material_layout() const { return m.material_layout; }

	static std::map<std::string, Pipeline> registry;
	static void rebuild_all(VFS &vfs);
};
