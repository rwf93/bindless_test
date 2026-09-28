#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "gfx/gfx.h"
#include "gfx/slang/slang.h"

namespace {

struct DescriptorHandle {
	uint32_t index;
	uint32_t offset;
};

int run() {
	auto instance = gfx::Instance::create({
		.application_name = "gfx_compute_example",
		.validation = true,
		.headless = true,
	});
	auto device = gfx::Device::create(instance, {.graphics = false});
	auto output = gfx::SharedBuffer<uint32_t>::create(device, 4);

	auto program = gfx::SlangProgram::create_from_source(
		"compute_example",
		R"(
		export T getDescriptorFromHandle<T>(DescriptorHandle<T> handle)
			where T : IOpaqueDescriptor
		{
			return defaultGetDescriptorFromHandle(
				handle,
				BindlessDescriptorOptions::None
			);
		}

		struct Bindings {
			RWStructuredBuffer<uint>.Handle output;
		};

		[vk::push_constant]
		ConstantBuffer<Bindings> bindings;

		[shader("compute")]
		[numthreads(4, 1, 1)]
		void compute_main(uint3 id : SV_DispatchThreadID)
		{
			RWStructuredBuffer<uint> values = bindings.output;
			values[id.x] = id.x * 3 + 1;
		}
	)"
	);
	auto pipeline = gfx::Pipeline::create_compute(device, program);

	device.submit_and_wait([&](gfx::CommandList &commands) {
		commands.bind_pipeline(pipeline.pipeline());
		const DescriptorHandle handle = {output.handle(), 0};
		commands.push_constants(&handle, sizeof(handle));
		commands.dispatch(1);
	});

	output.invalidate();
	constexpr std::array<uint32_t, 4> expected = {1, 4, 7, 10};
	for(size_t index = 0; index < expected.size(); index++) {
		if(output[index] != expected[index]) {
			throw std::runtime_error(
				"compute shader produced an unexpected result at index " +
				std::to_string(index)
			);
		}
	}

	std::cout << "compute example completed successfully\n";
	return 0;
}

} // namespace

int main() {
	try {
		return run();
	} catch(const std::exception &error) {
		std::cerr << "gfx_compute_example: " << error.what() << '\n';
		return 1;
	} catch(...) {
		std::cerr << "gfx_compute_example: unknown exception\n";
		return 1;
	}
}
