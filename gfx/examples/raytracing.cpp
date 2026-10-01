#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>

#include <SDL3/SDL.h>

#include "gfx/gfx.h"
#include "gfx/detail/types.h"
#include "gfx/framegraph/framegraph.h"
#include "gfx/present/presentation.h"
#include "gfx/raytracing/raytracing.h"
#include "gfx/upload_batch.h"
#include "gfx/sdl/surface.h"
#include "gfx/slang/slang.h"
#include "gfx/texture/texture_2d.h"

namespace {

struct DescriptorHandle {
	uint32_t index;
	uint32_t offset;
};

struct Constants {
	DescriptorHandle output;
	DescriptorHandle texture;
};

class SDLSession {
	explicit SDLSession() = default;

public:
	~SDLSession() { SDL_Quit(); }
	SDLSession(const SDLSession &) = delete;
	SDLSession &operator=(const SDLSession &) = delete;

	static SDLSession create() {
		if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
			throw std::runtime_error(SDL_GetError());
		return SDLSession();
	}
};

int run() {
	constexpr uint32_t window_width = 1280;
	constexpr uint32_t window_height = 720;
	auto sdl = SDLSession::create();
	std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
		SDL_CreateWindow(
			"gfx ray tracing example",
			window_width,
			window_height,
			SDL_WINDOW_VULKAN
		),
		&SDL_DestroyWindow
	);
	if(!window)
		throw std::runtime_error(SDL_GetError());

	auto instance = gfx::Instance::create({
		.application_name = "gfx_raytracing_example",
		.validation = true,
	});
	auto surface = gfx::sdl::Surface::create(instance, *window);
	auto device = gfx::Device::create(instance, {
		.surface = surface.native(),
		.frames_in_flight = 2,
		.graphics = true,
		.ray_tracing = true,
	});
	auto presentation = gfx::Presentation::create(
		device,
		surface.native(),
		{
			.width = window_width,
			.height = window_height,
		}
	);
	const VkExtent2D extent = presentation.extent();

	auto output = gfx::Texture2D::create(device, gfx::Texture2DDesc{
		.width = extent.width,
		.height = extent.height,
		.format = gfx::detail::from_vk_format(presentation.format()),
		.usage = gfx::TextureUsage::Storage | gfx::TextureUsage::TransferSource,
		.name = "raytracing_output",
	});

	const std::array<float, 9> vertices = {
		-0.75f, -0.75f, 0.0f,
		 0.75f, -0.75f, 0.0f,
		 0.00f,  0.75f, 0.0f,
	};
	const std::array<uint32_t, 3> indices = {0, 1, 2};

	auto build = gfx::UploadBatch::create(device);
	auto vertex_buffer = gfx::RawBuffer::create(device, {
		.size = sizeof(vertices),
		.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.device_address = true,
	});

	auto index_buffer = gfx::RawBuffer::create(device, {
		.size = sizeof(indices),
		.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.device_address = true,
	});

	build.upload(vertex_buffer, std::as_bytes(std::span(vertices)));
	build.upload(index_buffer, std::as_bytes(std::span(indices)));

	auto missing = gfx::Texture2D::generate(build, gfx::Texture2DDesc{
		.width = 128,
		.height = 128,
		.format = gfx::Format::R8G8B8A8Unorm,
		.usage = gfx::TextureUsage::Sampled,
	}, [](int x, int y) { return ((x / 16) + (y / 16)) % 2 == 0 ? 0x00ff0090u : 0x00000000u; });

	const gfx::raytracing::TriangleGeometry triangle{
		.vertex_address = vertex_buffer.address(),
		.vertex_count = 3,
		.vertex_stride = sizeof(float) * 3,
		.index_address = index_buffer.address(),
		.index_count = 3,
	};
	auto bottom_level = gfx::raytracing::AccelerationStructure::record_bottom_level(
		build,
		gfx::raytracing::BottomLevelDesc{
			.triangles = std::span<const gfx::raytracing::TriangleGeometry>(&triangle, 1),
		}
	);

	VkTransformMatrixKHR transform = {{
		{1.0f, 0.0f, 0.0f, 0.0f},
		{0.0f, 1.0f, 0.0f, 0.0f},
		{0.0f, 0.0f, 1.0f, 0.0f},
	}};
	const VkAccelerationStructureInstanceKHR instance_data{
		.transform = transform,
		.instanceCustomIndex = 0,
		.mask = 0xff,
		.instanceShaderBindingTableRecordOffset = 0,
		.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
		.accelerationStructureReference = bottom_level.device_address(),
	};
	auto top_level = gfx::raytracing::AccelerationStructure::record_top_level(
		build,
		gfx::raytracing::TopLevelDesc{
			.instances = std::span<const VkAccelerationStructureInstanceKHR>(&instance_data, 1),
		}
	);

	build.submit().wait();

	auto program = gfx::SlangProgram::create_from_source("raytracing_example", R"(
		export T getDescriptorFromHandle<T>(DescriptorHandle<T> handle)
			where T : IOpaqueDescriptor
		{
			return defaultGetDescriptorFromHandle(
				handle,
				BindlessDescriptorOptions::None
			);
		}

		[[vk::binding(8, 0)]]
		RaytracingAccelerationStructure scene;

		struct Constants {
			RWTexture2D<float4>.Handle output;
			Texture2D<float4>.Handle texture;
		};
		[vk::push_constant] ConstantBuffer<Constants> constants;

		struct Payload {
			float3 color;
			float2 uv;
		};

		[shader("raygeneration")]
		void raygen_main()
		{
			uint2 pixel = DispatchRaysIndex().xy;
			uint2 size = DispatchRaysDimensions().xy;
			float2 uv = (float2(pixel) + 0.5) / float2(size);

			RayDesc ray;
			ray.Origin = float3(uv * 2.0 - 1.0, 2.0);
			ray.Direction = float3(0.0, 0.0, -1.0);
			ray.TMin = 0.001;
			ray.TMax = 100.0;

			Payload payload;
			payload.uv = uv;

			TraceRay(scene, RAY_FLAG_FORCE_OPAQUE, 0xff, 0, 1, 0, ray, payload);
			RWTexture2D<float4> output = constants.output;
			output[pixel] = float4(payload.color, 1.0);
		}

		[shader("miss")]
		void miss_main(inout Payload payload)
		{
			payload.color = float3(0.02, 0.04, 0.08);
		}

		SamplerState sampler_linear_repeat() { return DescriptorHandle<SamplerState>(uint2(0, 0)); }

		[shader("closesthit")]
		void closest_hit_main(inout Payload payload, BuiltInTriangleIntersectionAttributes attributes)
		{
			Texture2D<float4> texture = constants.texture;
			float3 color = texture.SampleLevel(sampler_linear_repeat(), payload.uv, 0.0).rgb;
			payload.color = color;
		}
	)");
	auto pipeline = gfx::raytracing::Pipeline::create(
		device,
		program,
		gfx::raytracing::PipelineDesc{}
	);
	auto swapchain_target = gfx::FrameGraph::ExternalImage::create(
		"swapchain",
		gfx::FrameGraph::ImageDesc{
			.format = presentation.format(),
			.extent = {extent.width, extent.height, 1},
		},
		[&] { return presentation.image(); }
	);
	auto framegraph = gfx::FrameGraph::create(device)
		.add_ray_tracing_pass("trace",
			[&](gfx::FrameGraph::RayTracingPassBuilder &pass) {
				pass.read(top_level);
				pass.read(missing);
				pass.write(output);
			},
			[&](gfx::CommandList &commands) {
				const Constants constants{.output = {output.handle(), 0}, .texture = {missing.handle(), 0}};
				commands.set_root_data(&constants, sizeof(constants));
				pipeline.trace_rays(commands, extent.width, extent.height);
			}
		)
		.add_compute_pass("copy_to_swapchain",
			[&](gfx::FrameGraph::ComputePassBuilder &pass) {
				pass.read(output, gfx::FrameGraph::ImageAccess::TransferRead);
				pass.write(swapchain_target, gfx::FrameGraph::ImageAccess::TransferWrite);
			},
			[&](gfx::CommandList &commands) {
				VkImageCopy copy = {};
				copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				copy.srcSubresource.layerCount = 1;
				copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				copy.dstSubresource.layerCount = 1;
				copy.extent = {extent.width, extent.height, 1};
				device.dispatch().cmdCopyImage(
					commands.native(),
					output.image().image,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					presentation.image().image,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1,
					&copy
				);
			}
		)
		.compile();

	bool quit = false;
	while(!quit) {
		SDL_Event event;
		while(SDL_PollEvent(&event)) {
			if(event.type == SDL_EVENT_QUIT ||
				event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
			{
				quit = true;
			}
		}
		if(quit)
			break;

		auto commands = presentation.begin_frame();
		framegraph.execute(commands);
		const gfx::ImageRef target = presentation.image();
		commands.transition(
			target.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_IMAGE_ASPECT_COLOR_BIT
		);
		presentation.end_frame();
	}

	device.wait_idle();
	return 0;
}

} // namespace

int main() {
	try {
		return run();
	} catch(const std::exception &error) {
		std::cerr << "gfx_raytracing_example: " << error.what() << '\n';
		return 1;
	} catch(...) {
		std::cerr << "gfx_raytracing_example: unknown exception\n";
		return 1;
	}
}
