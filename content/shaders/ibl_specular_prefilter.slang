#include "shader_constants.slang"
#include "ibl_common.slang"

struct PrefilterConstants {
    TextureCube<float4>.Handle source_handle;
    RWTexture2DArray<float4>.Handle output_handle;
    uint32_t resolution;
    uint32_t sample_count;
    float roughness;
    uint32_t padding;
};

[vk::push_constant]
ConstantBuffer<PrefilterConstants> constants;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 thread_id: SV_DispatchThreadID) {
    if(thread_id.x >= constants.resolution ||
       thread_id.y >= constants.resolution ||
       thread_id.z >= 6)
        return;

    TextureCube<float4> source = constants.source_handle;
    RWTexture2DArray<float4> output = constants.output_handle;
    float2 uv = (float2(thread_id.xy) + 0.5) / float(constants.resolution);
    float3 normal = cube_face_direction(thread_id.z, uv);

    if(constants.roughness <= 0.0001) {
        output[thread_id] = float4(
            source.SampleLevel(sampler_linear_clamp(), normal, 0.0).rgb,
            1.0
        );
        return;
    }

    float3 view = normal;
    float3 radiance = float3(0.0);
    float total_weight = 0.0;
    for(uint i = 0; i < constants.sample_count; ++i) {
        float3 half_direction = importance_sample_ggx(
            hammersley(i, constants.sample_count),
            normal,
            constants.roughness
        );
        float3 light = normalize(2.0 * dot(view, half_direction) * half_direction - view);
        float ndotl = saturate(dot(normal, light));
        if(ndotl > 0.0) {
            radiance += source.SampleLevel(
                sampler_linear_clamp(),
                light,
                0.0
            ).rgb * ndotl;
            total_weight += ndotl;
        }
    }

    output[thread_id] = float4(
        radiance / max(total_weight, 0.0001),
        1.0
    );
}
