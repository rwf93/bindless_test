#pragma once

#include <vulkan/vulkan.h>
#include <slang.h>
#include <slang-com-ptr.h>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "gfx/device.h"

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
	std::vector<VkFormat> color_attachments;
	VkFormat depth_format = VK_FORMAT_UNDEFINED;
	VkCullModeFlagBits cull_mode = VK_CULL_MODE_NONE;
	bool depth_test = true;
	bool depth_write = true;
	VkCompareOp depth_compare = VK_COMPARE_OP_LESS;
};

class Pipeline {
	struct M {
		gfx::Device *device = nullptr;
		VkPipeline pipeline = VK_NULL_HANDLE;
	} m;

	explicit Pipeline(M m) : m(std::move(m)) {}

public:
	static Pipeline create_graphics(
		gfx::Device &device,
		SlangProgram &program,
		const GraphicsPipelineDesc &desc
	);
	static Pipeline create_compute(gfx::Device &device, SlangProgram &program);

	~Pipeline();

	Pipeline(const Pipeline &) = delete;
	Pipeline &operator=(const Pipeline &) = delete;
	Pipeline(Pipeline &&other) noexcept;
	Pipeline &operator=(Pipeline &&other) noexcept;

	VkPipeline pipeline() const { return m.pipeline; }
	gfx::Device &device() const { return *m.device; }
};

} // namespace gfx
