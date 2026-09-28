#pragma once

#include <vulkan/vulkan.h>

#include <utility>

namespace gfx {

class CommandList;
class Device;
class SlangProgram;
struct GraphicsPipelineDesc;

class Pipeline {
	struct M {
		gfx::Device *device = nullptr;
		VkPipeline pipeline = VK_NULL_HANDLE;
	} m;

	explicit Pipeline(M m) : m(std::move(m)) {}
	VkPipeline native() const { return m.pipeline; }
	friend class CommandList;

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

	gfx::Device &device() const { return *m.device; }
};

} // namespace gfx
