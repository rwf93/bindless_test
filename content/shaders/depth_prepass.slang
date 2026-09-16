#include "shader_constants.slang"

[vk::push_constant]
ConstantBuffer<ShaderConstants<uint32_t>> constants;

struct VertexOutput {
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertex_main(uint vertex_id : SV_VertexID, uint draw_id : SV_VulkanInstanceID) {
    Scene scene = constants.get_scene();
    Object object = constants.get_object(draw_id);
    uint index = constants.get_index(vertex_id);
    VertexData vertex = constants.get_vertex(index);

    float4 world_position = mul(object.model, vertex.position);
    float4x4 view_projection = mul(scene.projection, scene.view);

    VertexOutput output;
    output.position = mul(view_projection, world_position);
    return output;
}

[shader("fragment")]
void fragment_main() {}
