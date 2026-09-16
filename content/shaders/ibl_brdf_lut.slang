#include "shader_constants.slang"
#include "ibl_common.slang"

struct BRDFConstants {
    RWTexture2D<float4>.Handle output_handle;
    uint32_t width;
    uint32_t height;
    uint32_t sample_count;
    uint32_t padding;
};

[vk::push_constant]
ConstantBuffer<BRDFConstants> constants;

float geometry_schlick_ggx_ibl(float ndotx, float perceptual_roughness) {
    float alpha = perceptual_roughness * perceptual_roughness;
    float k = alpha * 0.5;
    return ndotx / max(ndotx * (1.0 - k) + k, 0.000001);
}

float2 integrate_brdf(float ndotv, float roughness) {
    float3 normal = float3(0.0, 0.0, 1.0);
    float3 view = float3(sqrt(max(1.0 - ndotv * ndotv, 0.0)), 0.0, ndotv);
    float scale = 0.0;
    float bias = 0.0;

    for(uint i = 0; i < constants.sample_count; ++i) {
        float3 half_direction = importance_sample_ggx(
            hammersley(i, constants.sample_count),
            normal,
            roughness
        );
        float3 light = normalize(2.0 * dot(view, half_direction) * half_direction - view);
        float ndotl = saturate(light.z);
        float ndoth = saturate(half_direction.z);
        float vdoth = saturate(dot(view, half_direction));

        if(ndotl > 0.0) {
            float geometry = geometry_schlick_ggx_ibl(ndotv, roughness)
                * geometry_schlick_ggx_ibl(ndotl, roughness);
            float geometry_visibility = geometry * vdoth
                / max(ndoth * ndotv, 0.000001);
            float fresnel = pow(1.0 - vdoth, 5.0);
            scale += (1.0 - fresnel) * geometry_visibility;
            bias += fresnel * geometry_visibility;
        }
    }

    return float2(scale, bias) / max(float(constants.sample_count), 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 thread_id: SV_DispatchThreadID) {
    if(thread_id.x >= constants.width || thread_id.y >= constants.height)
        return;

    RWTexture2D<float4> output = constants.output_handle;
    float2 uv = (float2(thread_id.xy) + 0.5) / float2(constants.width, constants.height);
    float2 integrated = integrate_brdf(max(uv.x, 0.0001), uv.y);
    output[thread_id.xy] = float4(integrated, 0.0, 1.0);
}
