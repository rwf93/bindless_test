struct Material {
    Texture2D.Handle albedo_handle;
    float hdri_enabled;
    float hdri_intensity;
    float hdri_rotation_degrees;
    float hdri_padding;
};

float3 sky_hash33(float3 p) {
    p = frac(p * float3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return frac((p.xxy + p.yxx) * p.zyx);
}

float sky_hash13(float3 p) {
    p = frac(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return frac((p.x + p.y) * p.z);
}

float sample_stars(float3 direction) {
    float3 p = direction * 150.0;
    float3 cell = floor(p);
    float3 local = frac(p);
    float stars = 0.0;

    [unroll]
    for(int z = -1; z <= 1; ++z) {
        [unroll]
        for(int y = -1; y <= 1; ++y) {
            [unroll]
            for(int x = -1; x <= 1; ++x) {
                float3 offset = float3(x, y, z);
                float3 candidate = offset + sky_hash33(cell + offset) - local;
                float brightness = smoothstep(0.15, 0.0, length(candidate));
                stars += brightness * step(0.92, sky_hash13(cell + offset));
            }
        }
    }
    return stars;
}

float3 sample_procedural_sky(float3 direction) {
    float3 dir = normalize(direction);
    float t = smoothstep(0.0, 0.4, dir.y);
    float3 top = float3(0.25, 0.25, 0.75);
    float3 horizon = float3(0.0);
    float3 sky = lerp(horizon, top - 0.50, t);
    float3 stars = float3(1.0, 1.0, 0.95) * sample_stars(dir);

    // The display path already clamps the old negative sky channels. Doing it
    // here also keeps the captured radiance physically meaningful for IBL.
    return max(sky + stars, float3(0.0));
}

float2 direction_to_equirectangular_uv(
    float3 direction,
    float rotation_degrees)
{
    float3 dir = normalize(direction);
    float2 uv = float2(
        atan2(dir.z, dir.x) / (2.0 * PI) + 0.5,
        0.5 - asin(clamp(dir.y, -1.0, 1.0)) / PI
    );
    uv.x = frac(uv.x + rotation_degrees / 360.0);
    return uv;
}

float3 sample_sky(Material material, float3 direction) {
    if(material.hdri_enabled >= 0.5 &&
        descriptor_handle_is_valid(material.albedo_handle))
    {
        float2 uv = direction_to_equirectangular_uv(
            direction,
            material.hdri_rotation_degrees
        );
        return max(
            material.albedo_handle.SampleLevel(
                sampler_linear_panorama(),
                uv,
                0.0
            ).rgb * max(material.hdri_intensity, 0.0),
            float3(0.0)
        );
    }

    return sample_procedural_sky(direction);
}
