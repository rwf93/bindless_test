#include "shader_constants.slangh"

// This shader's material parameters.
struct Material {
    uint32_t albedo_handle;
    uint32_t mrao_handle;
    uint32_t emission_handle;
    uint32_t normal_handle;
    float4 base_color_factor;
};

[vk::binding(0, 0)] StructuredBuffer<Material> typed_material[];

Material get_material() {
    return typed_material[constants.material_handle][constants.material];
}

struct VertexOutput {
    float4 position : SV_Position;
    float4 texcoord : TEXCOORD0;
    float4 normal : NORMAL0;
    float4 world_pos;
};

struct FragmentOutput {
    float4 color : SV_Target0;
    float4 normal : SV_Target1;
};

static float PI = 3.14159265359;

float3 aces_tonemap(float3 color) {
    // See https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
    const float3x3 ACES_INPUT_MAT = {
        0.59719, 0.35458, 0.04823,
        0.07600, 0.90834, 0.01566,
        0.02840, 0.13383, 0.83777
    };
    const float3x3 ACES_OUTPUT_MAT = {
         1.60475, -0.53108, -0.07367,
        -0.10208,  1.10813, -0.00605,
        -0.00327, -0.07276,  1.07602
    };

    color = mul(ACES_INPUT_MAT, color);

    // RRT and ODT fit from Stephen Hill
    float3 a = color * (color + 0.0245786) - 0.000090537;
    float3 b = color * (0.983729 * color + 0.432951) + 0.238081;
    color = a / b;

    color = mul(ACES_OUTPUT_MAT, color);

    // Clamp to [0, 1]
    color = saturate(color);

    return color;
}

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / denom;
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

[shader("vertex")]
VertexOutput vertex_main(uint vertex_id: SV_VertexID, uint draw_id: SV_VulkanInstanceID) {
    VertexOutput output;

    Scene scene = get_scene();
    Scene light_scene = get_scene();
    Object object = get_object(draw_id);

    uint index = get_index(vertex_id);
    VertexData vertex = get_vertex(index);

    float4x4 light_view_projection = mul(light_scene.projection, light_scene.view);

    float4 world_position = mul(object.model, vertex.position).xyzw;
    float4x4 view_projection = mul(scene.projection, scene.view);

    float3x3 normal_matrix = transpose(-float3x3(object.model));

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

    Material material = get_material();
    Scene scene = get_scene();

    float3 albedo = get_texture_2d(material.albedo_handle).Sample(input.texcoord.xy).rgb;
    albedo *= material.base_color_factor.rgb;
    float metallic = get_texture_2d(material.mrao_handle).Sample(input.texcoord.xy).r;
    float roughness = get_texture_2d(material.mrao_handle).Sample(input.texcoord.xy).g;
    float ao = get_texture_2d(material.mrao_handle).Sample(input.texcoord.xy).b;
    float3 emission = get_texture_2d(material.emission_handle).Sample(input.texcoord.xy).rgb;
    float3 normal_map = get_texture_2d(material.normal_handle).Sample(input.texcoord.xy).rgb;

    float3 N = normalize(input.normal.xyz);
    float3 V = normalize(scene.camera_position.xyz - input.world_pos.xyz);

    float3 F0 = float3(0.04);
    F0 = lerp(F0, albedo, metallic);

    float3 Lo = float3(0.0);

    for (int i = 0; i < 12; i++) {
        Light light = get_light(i);
        float3 L = normalize(light.position.xyz - input.world_pos.xyz);
        float3 H = normalize(V + L);

        float distance = length(light.position.xyz - input.world_pos.xyz);
        float attenuation = 1.0 / (distance * distance);
        float3 radiance = (light.color.rgb) * attenuation;

        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        float3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

        float3 kS = F;
        float3 kD = float3(1.0) - kS;
        kD *= 1.0 - metallic;

        float3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        float3 specular = numerator / denominator;

        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    float3 ambient = float3(0.03) * albedo * ao;
    float3 color = (emission.rgb) + ambient + Lo;

    // HDR tonemapping
    color = aces_tonemap(color);

    output.color = float4(color, 1.0);
    output.normal = input.normal;

    return output;
}