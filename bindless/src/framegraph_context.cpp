#include "framegraph.h"

void FrameGraph::PassContext::push_constants(
	VkShaderStageFlags stages,
	const void *data,
	size_t size
) {
	device().cmdPushConstants(
		m.cmd,
		global_layout,
		stages,
		0,
		uint32_t(size),
		data
	);
}

void FrameGraph::PassContext::bind_pipeline(VkPipeline pipeline) {
	device().cmdBindPipeline(m.cmd, m.bind_point, pipeline);
	device().cmdBindDescriptorSets(
		m.cmd,
		m.bind_point,
		global_layout,
		0,
		1,
		&bindless_desc[frame_index],
		0,
		nullptr
	);
}

void FrameGraph::PassContext::draw(
	uint32_t vertex_count,
	uint32_t instance_count,
	uint32_t first_vertex,
	uint32_t first_instance
) {
	device().cmdDraw(
		m.cmd,
		vertex_count,
		instance_count,
		first_vertex,
		first_instance
	);
}

void FrameGraph::PassContext::dispatch(
	uint32_t group_count_x,
	uint32_t group_count_y,
	uint32_t group_count_z
) {
	device().cmdDispatch(
		m.cmd,
		group_count_x,
		group_count_y,
		group_count_z
	);
}
