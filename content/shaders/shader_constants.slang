static float PI = 3.14159265359;
static uint32_t LIGHT_TYPE_POINT = 0;
static uint32_t LIGHT_TYPE_SPOT = 1;
static const uint32_t SHADOW_CASCADE_COUNT = 4;
static const uint32_t LOCAL_SHADOW_FACE_COUNT = 6;
static const uint32_t LOCAL_SHADOW_MAX_VIEW_COUNT = 64;
static const uint32_t LIGHT_PROBE_FACE_COUNT = 6;

struct VertexData {
    float4 position : POSITION;
    float4 texcoord : TEXCOORD0;
    float4 normal : NORMAL0;
};

struct LocalShadowMatrix {
    float4x4 view_projection;
};

struct LightProbe {
    float4x4 view_projection[LIGHT_PROBE_FACE_COUNT];
    float4 position;
};

struct ReflectionProbe {
    TextureCube<float4>.Handle specular_handle;
    TextureCube<float4>.Handle diffuse_handle;
    float4 position;
    float4 box_min;
    float4 box_max;
    // x: maximum specular LOD, y: intensity, z: priority, w: global fallback
    float4 parameters;
};

struct Scene {
    float4 camera_position;
    float4x4 projection;
    float4x4 view;
    float4x4 shadow_view_projection[SHADOW_CASCADE_COUNT];
    float4 shadow_split_depths;
    float4 shadow_light_direction;
    float4 shadow_light_color;
    Texture2DArray<float>.Handle shadowmap_handle;
    uint32_t shadow_cascade_count;
    uint32_t shadowmap_resolution;
    Texture2DArray<float>.Handle local_shadowmap_handle;
    uint32_t local_shadowmap_resolution;
    uint32_t light_count;
    StructuredBuffer<LocalShadowMatrix>.Handle local_shadow_matrix_handle;
    float2 local_shadow_padding;
    StructuredBuffer<ReflectionProbe>.Handle reflection_probe_handle;
    uint32_t reflection_probe_count;
    uint32_t reflection_probe_padding;
    Texture2D<float4>.Handle ibl_brdf_lut_handle;
    float2 ibl_padding;
};

struct Object {
    float4x4 model;
    uint4 local_shadow_layers[LOCAL_SHADOW_MAX_VIEW_COUNT / 16];
};

struct Light {
    float4 position;
    float4 direction;
    float4 color;
    uint32_t type;
    float range;
    float inner_cone_cos;
    float outer_cone_cos;
    uint32_t casts_shadow;
    float shadow_bias;
    float2 shadow_padding;
};

// Keep resource kinds in separate Vulkan bindings so texture and buffer slot
// numbers can overlap. The handles themselves are still native Slang
// DescriptorHandle<T> values and implicitly resolve to their resource type.
export T getDescriptorFromHandle<T>(DescriptorHandle<T> handle)
    where T : IOpaqueDescriptor
{
    return defaultGetDescriptorFromHandle(handle, BindlessDescriptorOptions::None);
}

struct ShaderConstants<TMaterial> {
    StructuredBuffer<VertexData>.Handle vbo_handle;
    StructuredBuffer<uint>.Handle ibo_handle;
    StructuredBuffer<Scene>.Handle scene_handle;
    StructuredBuffer<Object>.Handle object_handle;
    StructuredBuffer<Light>.Handle light_handle;
    StructuredBuffer<TMaterial>.Handle material_handle;
    StructuredBuffer<LightProbe>.Handle probe_handle;

    Scene get_scene() {
        return scene_handle[0];
    }

    Object get_object(uint32_t index) {
        return object_handle[index];
    }

    uint32_t get_local_shadow_layer(Object object, uint32_t visible_index) {
        uint32_t packed = object.local_shadow_layers[visible_index >> 4]
            [(visible_index >> 2) & 3];
        return (packed >> ((visible_index & 3) * 8)) & 0xff;
    }

    uint32_t get_index(uint32_t vertex_id) {
        return ((uint2)ibo_handle).x == uint32_t::maxValue
            ? vertex_id
            : ibo_handle[vertex_id];
    }

    VertexData get_vertex(uint32_t vertex_id) {
        return vbo_handle[vertex_id];
    }

    Light get_light(uint32_t index) {
        return light_handle[index];
    }

    TMaterial get_material() {
        return material_handle[0];
    }

    LightProbe get_probe() {
        return probe_handle[0];
    }
};

bool descriptor_handle_is_valid<T>(DescriptorHandle<T> handle)
    where T : IOpaqueDescriptor
{
    return ((uint2)handle).x != uint32_t::maxValue;
}

SamplerState sampler_linear_repeat() {
    return DescriptorHandle<SamplerState>(uint2(0, 0));
}

SamplerState sampler_linear_clamp() {
    return DescriptorHandle<SamplerState>(uint2(1, 0));
}

SamplerState sampler_nearest_repeat() {
    return DescriptorHandle<SamplerState>(uint2(2, 0));
}

SamplerState sampler_nearest_clamp() {
    return DescriptorHandle<SamplerState>(uint2(3, 0));
}

SamplerComparisonState sampler_shadow_compare() {
    return DescriptorHandle<SamplerComparisonState>(uint2(4, 0));
}

SamplerState sampler_linear_panorama() {
    return DescriptorHandle<SamplerState>(uint2(5, 0));
}

uint point_shadow_face(float3 light_to_surface)
{
    float3 axis = abs(light_to_surface);
    if(axis.x >= axis.y && axis.x >= axis.z)
        return light_to_surface.x >= 0.0 ? 0 : 1;
    if(axis.y >= axis.z)
        return light_to_surface.y >= 0.0 ? 2 : 3;
    return light_to_surface.z >= 0.0 ? 4 : 5;
}

float local_light_shadow_visibility(
    Scene scene,
    Light light,
    uint32_t light_index,
    float3 world_position,
    float3 N,
    float3 L
) {
    if(light.casts_shadow == 0 ||
       !descriptor_handle_is_valid(scene.local_shadowmap_handle) ||
       light_index >= scene.light_count)
        return 1.0;

    uint face = light.type == LIGHT_TYPE_POINT
        ? point_shadow_face(world_position - light.position.xyz)
        : 0;
    uint layer = light_index * LOCAL_SHADOW_FACE_COUNT + face;

    LocalShadowMatrix shadow_matrix = scene.local_shadow_matrix_handle[layer];
    float4 light_clip = mul(
        shadow_matrix.view_projection,
        float4(world_position, 1.0)
    );
    if(light_clip.w <= 0.0)
        return 1.0;

    float3 light_ndc = light_clip.xyz / light_clip.w;
    float2 shadow_uv = light_ndc.xy * 0.5 + 0.5;
    if(any(shadow_uv < float2(0.0)) || any(shadow_uv > float2(1.0)) ||
       light_ndc.z <= 0.0 || light_ndc.z >= 1.0)
        return 1.0;

    float bias = max(
        light.shadow_bias * (1.0 - saturate(dot(N, L))),
        light.shadow_bias * 0.1
    );
    Texture2DArray<float> shadow_map = scene.local_shadowmap_handle;
    SamplerComparisonState shadow_sampler = sampler_shadow_compare();

    static const float2 LOCAL_PCF_OFFSETS[4] = {
        float2(-0.75, -0.25),
        float2(0.25, -0.75),
        float2(0.75, 0.25),
        float2(-0.25, 0.75),
    };

    float visibility = 0.0;
    float texel_size = 1.0 / max(float(scene.local_shadowmap_resolution), 1.0);
    [unroll]
    for(uint i = 0; i < 4; ++i) {
        visibility += shadow_map.SampleCmpLevelZero(
            shadow_sampler,
            float3(shadow_uv + LOCAL_PCF_OFFSETS[i] * texel_size, float(layer)),
            light_ndc.z - bias
        );
    }
    return visibility * 0.25;
}

float cascaded_shadow_visibility(Scene scene, float3 world_position, float3 N, float3 L)
{
    if(!descriptor_handle_is_valid(scene.shadowmap_handle))
        return 1.0;

    float view_depth = -mul(scene.view, float4(world_position, 1.0)).z;
    if(view_depth <= 0.0 || view_depth > scene.shadow_split_depths.w)
        return 1.0;

    uint cascade = 0;
    cascade += view_depth > scene.shadow_split_depths.x ? 1 : 0;
    cascade += view_depth > scene.shadow_split_depths.y ? 1 : 0;
    cascade += view_depth > scene.shadow_split_depths.z ? 1 : 0;

    float4 light_clip = mul(
        scene.shadow_view_projection[cascade],
        float4(world_position, 1.0)
    );
    float3 light_ndc = light_clip.xyz / light_clip.w;
    float2 shadow_uv = light_ndc.xy * 0.5 + 0.5;

    if(any(shadow_uv < float2(0.0)) || any(shadow_uv > float2(1.0)) ||
       light_ndc.z <= 0.0 || light_ndc.z >= 1.0)
        return 1.0;

    const float bias = max(0.00075 * (1.0 - saturate(dot(N, L))), 0.000075);

    Texture2DArray<float> shadow_map = scene.shadowmap_handle;
    SamplerComparisonState shadow_sampler = sampler_shadow_compare();

    static const float2 PCF_OFFSETS[4] = {
        float2(-0.75, -0.25),
        float2(0.25, -0.75),
        float2(0.75, 0.25),
        float2(-0.25, 0.75),
    };

    float visibility = 0.0;
    float2 texel_size = 1.0 / float(scene.shadowmap_resolution);
    float filter_radius = 1.5;

    [unroll]
    for (uint i = 0; i < 4; ++i) {
        float2 uv = shadow_uv +
                    PCF_OFFSETS[i] * texel_size * filter_radius;

        visibility += shadow_map.SampleCmpLevelZero(
            shadow_sampler,
            float3(uv, float(cascade)),
            light_ndc.z - bias
        );
    }

    return visibility * 0.25;
}
