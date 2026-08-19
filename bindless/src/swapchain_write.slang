#include "shader_constants.slangh"

SHADER_MATERIAL_PARAMS {
    uint32_t albedo_handle;
    uint32_t normal_handle;
    uint32_t depth_handle;
};

struct VertexOutput {
    float4 position : SV_Position;
    float4 texcoord : TEXCOORD0;
};

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("vertex")]
VertexOutput vertex_main(uint vertex_id: SV_VertexID, uint draw_id: SV_VulkanInstanceID) {
    VertexOutput output;
    output.texcoord = float4((vertex_id << 1) & 2, vertex_id & 2, 0, 0);
    output.position = float4(output.texcoord.xy * 2.0f - 1.0f, 0, 1.0);
    return output;
}

[shader("fragment")]
FragmentOutput fragment_main(VertexOutput input) {
    FragmentOutput output;

    Material material = get_material();
    float3 color = get_texture_2d(material.albedo_handle).Sample(input.texcoord.xy).rgb;

    //color = pow(color, float3(1 / 2.4));

    output.color = float4(color, 1.0);
    return output;
}