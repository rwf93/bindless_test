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

// hack to override cause i hate cout
template <typename T, std::size_t N>
std::ostream& operator<<(std::ostream& os, const std::array<T, N>& arr) {
    os << "[";
    for (std::size_t i = 0; i < N; ++i) {
        os << arr[i];
        if (i < N - 1) os << ", ";
    }
    os << "]";
    return os;
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const gfx::SharedBuffer<T>& arr) {
    os << "[";
	for(std::size_t i = 0; i < arr.count(); i++) {
		os << arr[i];
		if (i < arr.count() - 1) os << ", ";
	}
    os << "]";
    return os;
}


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
		commands.set_pipeline(pipeline);
		const DescriptorHandle handle = {output.handle(), 0};
		commands.set_root_data(&handle, sizeof(handle));
		commands.dispatch(1);
	});

	output.invalidate();
	constexpr std::array<uint32_t, 4> expected = {1, 4, 7, 10};
	std::cout << "expected: " << expected << "\n";
	std::cout << "got: " << output << "\n";

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
