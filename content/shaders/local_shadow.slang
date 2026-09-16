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
    const uint visible_index = packed_instance % LOCAL_SHADOW_MAX_VIEW_COUNT;
    const uint object_index = packed_instance / LOCAL_SHADOW_MAX_VIEW_COUNT;
    Object object = constants.get_object(object_index);
    const uint layer = constants.get_local_shadow_layer(object, visible_index);

    VertexOutput output;
    output.layer = layer;
    uint index = constants.get_index(vertex_id);
    VertexData vertex = constants.get_vertex(index);
    float4 world_position = mul(object.model, vertex.position);

    LocalShadowMatrix shadow_matrix = scene.local_shadow_matrix_handle[layer];
    output.position = mul(shadow_matrix.view_projection, world_position);
    return output;
}

[shader("fragment")]
void fragment_main() {}
