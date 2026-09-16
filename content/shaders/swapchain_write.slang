#include "shader_constants.slang"

struct Material {
    Texture2D.Handle albedo_handle;
};

[vk::push_constant]
ConstantBuffer<ShaderConstants<Material>> constants;

struct VertexOutput {
    float4 position : SV_Position;
    float4 texcoord : TEXCOORD0;
};

struct FragmentOutput {
    float4 color : SV_Target0;
};

float3 aces_tonemap(float3 color) {
    const float3x3 input_transform = {
        0.59719, 0.35458, 0.04823,
        0.07600, 0.90834, 0.01566,
        0.02840, 0.13383, 0.83777
    };
    const float3x3 output_transform = {
         1.60475, -0.53108, -0.07367,
        -0.10208,  1.10813, -0.00605,
        -0.00327, -0.07276,  1.07602
    };

    color = mul(input_transform, color);
    float3 numerator = color * (color + 0.0245786) - 0.000090537;
    float3 denominator = color * (0.983729 * color + 0.432951) + 0.238081;
    return saturate(mul(output_transform, numerator / denominator));
}

float3 linear_to_srgb(float3 color) {
    float3 low = color * 12.92;
    float3 high = 1.055 * pow(max(color, float3(0.0)), 1.0 / 2.4) - 0.055;
    return lerp(low, high, step(float3(0.0031308), color));
}

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

    Material material = constants.get_material();
    float3 hdr_color = material.albedo_handle.Sample(sampler_nearest_clamp(), input.texcoord.xy).rgb;
    float3 color = max(hdr_color, float3(0.0));

    output.color = float4(linear_to_srgb(color), 1.0);
    return output;
}
