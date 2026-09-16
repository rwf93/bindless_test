#include "shader_constants.slang"
#include "ibl_common.slang"
#include "sky_common.slang"

[vk::push_constant]
ConstantBuffer<ShaderConstants<Material>> constants;

struct VertexOutput {
    float4 position : SV_Position;
    float3 direction : TEXCOORD0;
    uint layer : SV_RenderTargetArrayIndex;
};

[shader("vertex")]
VertexOutput vertex_main(uint vertex_id: SV_VertexID, uint face: SV_VulkanInstanceID) {
    float2 triangle_uv = float2((vertex_id << 1) & 2, vertex_id & 2);
    float2 ndc = triangle_uv * 2.0 - 1.0;

    VertexOutput output;
    output.position = float4(ndc, 1.0, 1.0);
    output.direction = cube_face_direction(face, triangle_uv);
    output.layer = face;
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target0 {
    Material material = constants.get_material();
    return float4(sample_sky(material, input.direction), 1.0);
}
