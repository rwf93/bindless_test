#include <iostream>
#include <stdexcept>
#include <gfx/gfx.h>
#include <gfx/slang/slang.h>

struct GPUHandle {
	uint32_t index;
	uint32_t offset;
};

struct Bindings {
	GPUHandle buffer;
	GPUHandle image;
};

int run() {
	auto instance = gfx::Instance::create({
		.validation = true,
		.headless = true,
	});
	auto device = gfx::Device::create(instance, {.graphics = false});

	auto image = gfx::Texture2D::create(device, gfx::Texture2DDesc{
		.width = 128,
		.height = 128,
		.format = gfx::Format::R8G8B8A8Unorm,
		.usage = gfx::TextureUsage::Storage,
		.name = "compute_output",
	});

	auto buffer = gfx::SharedBuffer<uint32_t>::create(device);

	auto program = gfx::SlangProgram::create_from_source("compute_shader", R"(
		// Because gfx has its own default binding layout, we gotta override it in the shader :(
		export T getDescriptorFromHandle<T>(DescriptorHandle<T> handle) where T : IOpaqueDescriptor {
			return defaultGetDescriptorFromHandle(handle, BindlessDescriptorOptions::None);
		}

		struct Bindings {
			RWStructuredBuffer<uint32_t>.Handle buffer;
			RWTexture2D<float4>.Handle image;
		};
		[vk::push_constant] ConstantBuffer<Bindings> constants;

		[shader("compute")]
		[numthreads(8, 8, 1)]
		void main(uint3 id : SV_DispatchThreadID)
		{
			RWStructuredBuffer<uint32_t> buffer = constants.buffer;
			RWTexture2D<float4> image = constants.image;
			image[id.xy] = float4(
				float(id.x) / 127.0,
				float(id.y) / 127.0,
				0.25,
				1.0
			);
			if(all(id == uint3(0)))
				buffer[0] = 42;
		}
	)");

	auto pipeline = gfx::Pipeline::create_compute(device, program);

	device.submit_and_wait([&](gfx::CommandList &commands) {
		commands.set_pipeline(pipeline);
		const Bindings bindings = {
			.buffer = {buffer.handle(), 0},
			.image = {image.handle(), 0},
		};
		commands.set_root_data(&bindings, sizeof(bindings));
		commands.dispatch(128 / 8, 128 / 8);
	});

	buffer.invalidate();
	if(buffer[0] != 42)
		throw std::runtime_error("compute shader did not write the expected value");

	return 0;
}

int main() {
	try {
		return run();
	} catch(const std::exception &error) {
		std::cerr << "compute_test: " << error.what() << '\n';
		return 1;
	} catch(...) {
		std::cerr << "compute_test: unknown exception\n";
		return 1;
	}
}
