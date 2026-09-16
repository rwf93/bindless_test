#include "shader_constants.slang"

struct Material {
    Texture2D.Handle albedo_handle;
    Texture2D.Handle mrao_handle;
    Texture2D.Handle emission_handle;
    Texture2D.Handle normal_handle;
    float4 base_color_factor;
};

#include "pbr_brdf.slang"
#include "pbr_lighting.slang"

[vk::push_constant]
ConstantBuffer<ShaderConstants<Material>> constants;

struct VertexOutput {
    float4 position : SV_Position;
    float4 texcoord : TEXCOORD0;
    float4 normal : NORMAL0;
    float4 world_position : TEXCOORD1;
    uint layer : SV_RenderTargetArrayIndex;
};

[shader("vertex")]
VertexOutput vertex_main(
    uint vertex_id: SV_VertexID,
    uint packed_instance: SV_VulkanInstanceID
) {
    const uint face = packed_instance % 6;
    const uint object_index = packed_instance / 6;
    LightProbe probe = constants.get_probe();
    Object object = constants.get_object(object_index);
    uint index = constants.get_index(vertex_id);
    VertexData vertex = constants.get_vertex(index);
    float4 world_position = mul(object.model, vertex.position);

    VertexOutput output;
    output.position = mul(probe.view_projection[face], world_position);
    output.texcoord = vertex.texcoord;
    output.normal = float4(
        normalize(mul((float3x3)object.model, vertex.normal.xyz)),
        0.0
    );
    output.world_position = world_position;
    output.layer = face;
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target0 {
    Scene scene = constants.get_scene();
    LightProbe probe = constants.get_probe();
    Material material = constants.get_material();
    PBRSurface surface = material.sample_surface(
        input.texcoord.xy,
        input.normal.xyz,
        input.world_position.xyz
    );
    PBRView view = StandardPBR.prepare_view(
        surface,
        probe.position.xyz - input.world_position.xyz
    );
    float3 direct = evaluate_scene_direct_lighting(
        constants,
        scene,
        surface,
        view,
        input.world_position.xyz
    );
    return float4(surface.emission + direct, 1.0);
}
