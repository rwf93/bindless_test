#include "shader_constants.slang"
#include "ibl_common.slang"

struct IrradianceConstants {
    TextureCube<float4>.Handle source_handle;
    RWTexture2DArray<float4>.Handle output_handle;
    uint32_t resolution;
    uint32_t sample_count;
};

[vk::push_constant]
ConstantBuffer<IrradianceConstants> constants;

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
    float3 irradiance = float3(0.0);

    for(uint i = 0; i < constants.sample_count; ++i) {
        float3 light = cosine_sample_hemisphere(
            hammersley(i, constants.sample_count),
            normal
        );
        irradiance += source.SampleLevel(
            sampler_linear_clamp(),
            light,
            0.0
        ).rgb;
    }

    irradiance *= PI / max(float(constants.sample_count), 1.0);
    output[thread_id] = float4(irradiance, 1.0);
}
