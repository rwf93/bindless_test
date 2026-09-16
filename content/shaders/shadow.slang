#include "shader_constants.slang"

[vk::push_constant]
ConstantBuffer<ShaderConstants<uint32_t>> constants;

struct VertexOutput {
	float4 position : SV_Position;
	uint layer : SV_RenderTargetArrayIndex;
};

[shader("vertex")]
VertexOutput vertex_main(uint vertex_id: SV_VertexID, uint packed_instance: SV_VulkanInstanceID) {
    Scene scene = constants.get_scene();

    const uint cascade = packed_instance % 4;
    const uint object_index = packed_instance / 4;

    Object object = constants.get_object(object_index);
	uint index = constants.get_index(vertex_id);
	VertexData vertex = constants.get_vertex(index);
	float4 world_position = mul(object.model, vertex.position);

	VertexOutput output;
	output.position = mul(scene.shadow_view_projection[cascade], world_position);
	output.layer = cascade;
	return output;
}

[shader("fragment")]
void fragment_main() {}
