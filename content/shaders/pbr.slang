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
    float4 world_pos;
};

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("vertex")]
VertexOutput vertex_main(uint vertex_id: SV_VertexID, uint draw_id: SV_VulkanInstanceID) {
    VertexOutput output;

    Scene scene = constants.get_scene();
    Object object = constants.get_object(draw_id);

    uint index = constants.get_index(vertex_id);
    VertexData vertex = constants.get_vertex(index);

    float4 world_position = mul(object.model, vertex.position).xyzw;
    float4x4 view_projection = mul(scene.projection, scene.view);

    output.position = mul(view_projection, world_position);
    output.texcoord = vertex.texcoord;
    output.normal = vertex.normal.xyzw;
    output.normal = float4(mul((float3x3)object.model, vertex.normal.xyz), 1.0);
    output.world_pos = world_position;

    return output;
}

[shader("fragment")]
FragmentOutput fragment_main(VertexOutput input) {
    FragmentOutput output;

    Material material = constants.get_material();
    Scene scene = constants.get_scene();

    PBRSurface surface = material.sample_surface(
        input.texcoord.xy,
        input.normal.xyz,
        input.world_pos.xyz);

    PBRView view = StandardPBR.prepare_view(
        surface,
        scene.camera_position.xyz - input.world_pos.xyz);

    float3 Lo = evaluate_scene_direct_lighting(
        constants,
        scene,
        surface,
        view,
        input.world_pos.xyz
    );

    float3 ambient = evaluate_probe_ibl(
        scene,
        surface,
        view,
        input.world_pos.xyz
    );
    float3 color = surface.emission + ambient + Lo;

    output.color = float4(color, 1.0);
    return output;
}
