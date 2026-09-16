#include "shader_constants.slang"

struct ComputeConstants {
    RWTexture2D<float4>.Handle output;
    uint32_t width;
    uint32_t height;
};

[vk::push_constant] ConstantBuffer<ComputeConstants> constants;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 thread_id: SV_DispatchThreadID) {
    if (thread_id.x >= constants.width || thread_id.y >= constants.height)
        return;

    RWTexture2D<float4> output = constants.output;
    output[thread_id.xy] = (thread_id.x % 2 == 0) || (thread_id.y % 2 == 0) ? float4(0, 0, 0, 1) : float4(1, 1, 1, 0);
}