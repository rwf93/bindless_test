#include "shader_constants.slangh"

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
    Scene light_scene = get_light_scene();
    Object object = get_object(draw_id);

    uint index = get_index(vertex_id);
    VertexData vertex = get_vertex(index);

    float4x4 light_view_projection = mul(light_scene.projection, light_scene.view);

    float4 world_position = mul(object.model, vertex.position).xyzw;
    float4x4 view_projection = mul(scene.projection, scene.view);

    output.position = mul(view_projection, world_position);
    output.texcoord = vertex.texcoord;
    output.normal = vertex.normal;
    output.world_pos = world_position;

    return output;
}

[shader("fragment")]
FragmentOutput fragment_main(VertexOutput input) {
    FragmentOutput output;

    Material material = get_material();
    Scene scene = get_scene();

    float3 N = normalize(input.normal.xyz);
    float3 V = normalize(scene.camera_position.xyz - input.world_pos.xyz);

    float3 albedo = get_texture_2d(material.albedo_handle).Sample(input.texcoord.xy).rgb;

    float3 F0 = float3(0.04);
    F0 = lerp(F0, albedo, material.metallic);

    float3 Lo = float3(0.0);

    for (int i = 0; i < 12; i++) {
        Light light = get_light(i);
        float3 L = normalize(light.position.xyz - input.world_pos.xyz);
        float3 H = normalize(V + L);

        float distance = length(light.position.xyz - input.world_pos.xyz);
        float attenuation = 1.0 / (distance * distance);
        float3 radiance = (light.color.rgb * 255) * attenuation;

        float NDF = DistributionGGX(N, H, material.roughness);
        float G = GeometrySmith(N, V, L, material.roughness);
        float3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

        float3 kS = F;
        float3 kD = float3(1.0) - kS;
        kD *= 1.0 - material.metallic;

        float3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        float3 specular = numerator / denominator;

        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    float3 ambient = float3(0.03) * albedo * material.ao;
    float3 color = ambient + Lo;

    color = color / (color + float3(1.0));
    color = pow(color, float3(1.0 / 2.2));

    output.color = float4(color, 1.0);
    output.normal = input.normal;

    return output;
}