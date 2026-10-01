#include "gfx/present/presentation.h"

#include <stdexcept>
#include <string>

#include "gfx/device.h"
#include "gfx/vkinfo.h"
#include "gfx/vktools.h"

namespace gfx {

Presentation Presentation::create(
	Device &device,
	VkSurfaceKHR surface,
	const PresentationDesc &desc
) {
	if(surface == VK_NULL_HANDLE)
		throw std::invalid_argument("gfx::Presentation::create: surface is null");
	if(!device.graphics_capable()) {
		throw std::invalid_argument(
			"gfx::Presentation::create: device is not graphics capable"
		);
	}

	VkSurfaceFormatKHR preferred = {
		.format = desc.preferred_format,
		.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
	};
	VkSurfaceFormatKHR fallback = {
		.format = desc.fallback_format,
		.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
	};
	auto builder = vkb::SwapchainBuilder{device.bootstrap()}
		.set_desired_format(preferred)
		.add_fallback_format(fallback)
		.set_desired_present_mode(desc.present_mode)
		.set_desired_min_image_count(device.frames_in_flight())
		.add_image_usage_flags(
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_TRANSFER_DST_BIT
		);
	if(desc.width != 0 && desc.height != 0)
		builder.set_desired_extent(desc.width, desc.height);

	auto swapchain_result = builder.build();
	if(!swapchain_result) {
		throw std::runtime_error(
			"gfx::Presentation::create: " +
			std::string(swapchain_result.error().message())
		);
	}
	auto swapchain = swapchain_result.value();
	auto images = swapchain.get_images().value();
	auto views = swapchain.get_image_views().value();

	auto pool_info = info::command_pool_create_info(
		device.queue_family(),
		VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
	);
	VkCommandPool pool = VK_NULL_HANDLE;
	VK_CHECK(device.dispatch().createCommandPool(&pool_info, nullptr, &pool));

	std::vector<Frame> frames(device.frames_in_flight());
	VkSemaphoreCreateInfo semaphore_info = {};
	semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	VkFenceCreateInfo fence_info = {};
	fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	auto allocate_info = info::command_buffer_allocate_info(
		pool,
		uint32_t(frames.size())
	);
	std::vector<VkCommandBuffer> commands(frames.size());
	VK_CHECK(device.dispatch().allocateCommandBuffers(
		&allocate_info,
		commands.data()
	));
	for(size_t index = 0; index < frames.size(); index++) {
		frames[index].command = commands[index];
		VK_CHECK(device.dispatch().createFence(
			&fence_info,
			nullptr,
			&frames[index].fence
		));
		VK_CHECK(device.dispatch().createSemaphore(
			&semaphore_info,
			nullptr,
			&frames[index].acquire
		));
	}
	std::vector<VkSemaphore> render_finished(images.size(), VK_NULL_HANDLE);
	for(VkSemaphore &semaphore : render_finished) {
		VK_CHECK(device.dispatch().createSemaphore(
			&semaphore_info,
			nullptr,
			&semaphore
		));
	}

	std::vector<ImageId> image_ids;
	image_ids.reserve(images.size());
	for(size_t index = 0; index < images.size(); index++)
		image_ids.push_back(detail::allocate_image_id());

	return Presentation(M{
		.device = &device,
		.swapchain = swapchain,
		.images = std::move(images),
		.views = std::move(views),
		.image_ids = std::move(image_ids),
		.command_pool = pool,
		.frames = std::move(frames),
		.render_finished = std::move(render_finished),
	});
}

Presentation::~Presentation() {
	if(!m.device)
		return;
	m.device->wait_idle();
	for(const Frame &frame : m.frames) {
		if(frame.fence != VK_NULL_HANDLE)
			m.device->dispatch().destroyFence(frame.fence, nullptr);
		if(frame.acquire != VK_NULL_HANDLE)
			m.device->dispatch().destroySemaphore(frame.acquire, nullptr);
	}
	for(VkSemaphore semaphore : m.render_finished) {
		if(semaphore != VK_NULL_HANDLE)
			m.device->dispatch().destroySemaphore(semaphore, nullptr);
	}
	if(m.command_pool != VK_NULL_HANDLE)
		m.device->dispatch().destroyCommandPool(m.command_pool, nullptr);
	if(!m.views.empty())
		m.swapchain.destroy_image_views(m.views);
	if(m.swapchain.swapchain != VK_NULL_HANDLE)
		vkb::destroy_swapchain(m.swapchain);
}

CommandList Presentation::begin_frame() {
	if(m.recording)
		throw std::logic_error("gfx::Presentation::begin_frame: frame already recording");
	Frame &frame = m.frames[m.frame_index];
	VK_CHECK(m.device->dispatch().waitForFences(
		1,
		&frame.fence,
		VK_TRUE,
		UINT64_MAX
	));
	VK_CHECK(m.device->dispatch().resetFences(1, &frame.fence));
	VK_CHECK(m.device->dispatch().resetCommandBuffer(frame.command, 0));

	const VkResult acquired = m.device->dispatch().acquireNextImageKHR(
		m.swapchain,
		UINT64_MAX,
		frame.acquire,
		VK_NULL_HANDLE,
		&m.image_index
	);
	if(acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR)
		throw std::runtime_error("gfx::Presentation::begin_frame: image acquisition failed");

	VkCommandBufferBeginInfo begin = {};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	VK_CHECK(m.device->dispatch().beginCommandBuffer(frame.command, &begin));
	m.device->set_frame_index(m.frame_index);
	m.recording = true;
	return CommandList::create(*m.device, frame.command);
}

void Presentation::end_frame() {
	if(!m.recording)
		throw std::logic_error("gfx::Presentation::end_frame: no frame is recording");
	Frame &frame = m.frames[m.frame_index];
	VK_CHECK(m.device->dispatch().endCommandBuffer(frame.command));
	const VkSemaphore render_finished = m.render_finished.at(m.image_index);

	auto command_info = info::command_buffer_submit_info(frame.command);
	auto wait_info = info::semaphore_submit_info(
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		frame.acquire
	);
	auto signal_info = info::semaphore_submit_info(
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		render_finished
	);
	auto submit_info = info::submit_info(
		&command_info,
		&signal_info,
		&wait_info
	);
	VK_CHECK(m.device->dispatch().queueSubmit2(
		m.device->queue(),
		1,
		&submit_info,
		frame.fence
	));

	VkPresentInfoKHR present = {};
	present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present.swapchainCount = 1;
	present.pSwapchains = &m.swapchain.swapchain;
	present.waitSemaphoreCount = 1;
	present.pWaitSemaphores = &render_finished;
	present.pImageIndices = &m.image_index;
	const VkResult presented = m.device->dispatch().queuePresentKHR(
		m.device->present_queue(),
		&present
	);
	if(presented != VK_SUCCESS && presented != VK_SUBOPTIMAL_KHR)
		throw std::runtime_error("gfx::Presentation::end_frame: presentation failed");

	m.recording = false;
	m.frame_index = (m.frame_index + 1) % uint32_t(m.frames.size());
}

ImageRef Presentation::image() const {
	return ImageRef{
		.id = m.image_ids.at(m.image_index),
		.image = m.images.at(m.image_index),
		.view = m.views.at(m.image_index),
		.desc = ImageDesc{
			.image_type = VK_IMAGE_TYPE_2D,
			.view_type = VK_IMAGE_VIEW_TYPE_2D,
			.format = m.swapchain.image_format,
			.extent = {m.swapchain.extent.width, m.swapchain.extent.height, 1},
			.usage = m.swapchain.image_usage_flags,
		},
		.subresources = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};
}

} // namespace gfx
