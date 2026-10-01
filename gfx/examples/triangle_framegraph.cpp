#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <SDL3/SDL.h>

#include "gfx/command_list.h"
#include "gfx/detail/types.h"
#include "gfx/device.h"
#include "gfx/instance.h"
#include "gfx/present/presentation.h"
#include "gfx/sdl/surface.h"
#include "gfx/slang/slang.h"
#include "gfx/framegraph/framegraph.h"

namespace {

constexpr uint32_t window_width = 1280;
constexpr uint32_t window_height = 720;

class SDLSession {
	explicit SDLSession() = default;

public:
	~SDLSession() { SDL_Quit(); }
	SDLSession(const SDLSession &) = delete;
	SDLSession &operator=(const SDLSession &) = delete;
	SDLSession(SDLSession &&) = delete;
	SDLSession &operator=(SDLSession &&) = delete;

	static SDLSession create() {
		if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
			throw std::runtime_error(SDL_GetError());
		return SDLSession();
	}
};

int run() {
	auto sdl = SDLSession::create();
	std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
		SDL_CreateWindow(
			"gfx triangle example",
			window_width,
			window_height,
			SDL_WINDOW_VULKAN
		),
		&SDL_DestroyWindow
	);
	if(!window)
		throw std::runtime_error(SDL_GetError());

	auto instance = gfx::Instance::create({
		.application_name = "gfx_triangle_example",
		.validation = true,
	});
	auto surface = gfx::sdl::Surface::create(instance, *window);
	auto device = gfx::Device::create(instance, {
		.surface = surface.native(),
		.frames_in_flight = 3,
		.graphics = true,
	});
	auto presentation = gfx::Presentation::create(
		device,
		surface.native(),
		{
			.width = window_width,
			.height = window_height,
		}
	);

	auto program = gfx::SlangProgram::create_from_source(
		"triangle_example",
		R"(
		struct VertexOutput {
			float4 position : SV_Position;
			float3 color : COLOR0;
		};

		[shader("vertex")]
		VertexOutput vertex_main(uint vertex_id : SV_VertexID)
		{
			float2 positions[3] = {
				float2( 0.0, -0.7),
				float2( 0.7,  0.7),
				float2(-0.7,  0.7)
			};
			float3 colors[3] = {
				float3(1.0, 0.2, 0.1),
				float3(0.1, 1.0, 0.2),
				float3(0.2, 0.3, 1.0)
			};

			VertexOutput output;
			output.position = float4(positions[vertex_id], 0.0, 1.0);
			output.color = colors[vertex_id];
			return output;
		}

		[shader("fragment")]
		float4 fragment_main(VertexOutput input) : SV_Target
		{
			return float4(input.color, 1.0);
		}
	)"
	);

	auto pipeline = gfx::Pipeline::create_graphics(
		device,
		program,
		gfx::GraphicsPipelineDesc{
			.color_attachments = {
				gfx::detail::from_vk_format(presentation.format())
			},
			.depth_test = false,
			.depth_write = false,
		}
	);

	auto swapchain_target = gfx::FrameGraph::ExternalImage::create(
		"swapchain",
		gfx::FrameGraph::ImageDesc{
			.format = presentation.format(),
			.extent = {
				presentation.extent().width,
				presentation.extent().height,
				1,
			},
		},
		[&] {
			return presentation.image();
		}
	);

	auto framegraph = gfx::FrameGraph::create(device)
		.add_render_pass("forward",
			[&](gfx::FrameGraph::RenderPassBuilder &pass){
				pass.clear_color(swapchain_target, {0,0,0,1});
			},
			[&](gfx::CommandList &cmd) {
				cmd.viewport(presentation.extent().width, presentation.extent().height);
				cmd.set_pipeline(pipeline);
				cmd.draw(3);
			}
		).compile();

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
		const gfx::ImageRef target = presentation.image();

		framegraph.execute(commands);

		commands.transition(
			target.image,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
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
		std::cerr << "gfx_triangle_example: " << error.what() << '\n';
		return 1;
	} catch(...) {
		std::cerr << "gfx_triangle_example: unknown exception\n";
		return 1;
	}
}
