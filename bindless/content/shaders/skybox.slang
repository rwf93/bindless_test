#include "shader_constants.slangh"

struct VertexOutput {
    float4 position : SV_Position;
    float3 direction : TEXCOORD0;
};

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("vertex")]
VertexOutput vertex_main(uint vertex_id: SV_VertexID, uint draw_id: SV_VulkanInstanceID) {
    VertexOutput output;

    float2 uv = float2((vertex_id << 1) & 2, vertex_id & 2);
    float2 ndc = uv * 2.0f - 1.0f;
    output.position = float4(ndc, 1.0f, 1.0f);

    Scene scene = get_scene();
    float3 view_ray = float3(
        ndc.x / scene.projection[0][0],
        ndc.y / scene.projection[1][1],
        -1.0f
    );
    output.direction = mul(view_ray, (float3x3)scene.view);

    return output;
}

[shader("fragment")]
FragmentOutput fragment_main(VertexOutput input) {
    FragmentOutput output;

    float3 dir = normalize(input.direction);
    float t = smoothstep(0.0f, 0.4f, dir.y);

    float3 top    = float3(0.15f, 0.35f, 0.75f);
    float3 horizon = float3(0.75f, 0.85f, 1.0f);

    output.color = float4(lerp(horizon, top, t), 1.0f);
    return output;
}
