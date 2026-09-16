float3 cube_face_direction(uint face, float2 uv) {
    float2 p = uv * 2.0 - 1.0;
    switch(face) {
    case 0: return normalize(float3( 1.0, -p.y, -p.x));
    case 1: return normalize(float3(-1.0, -p.y,  p.x));
    case 2: return normalize(float3( p.x,  1.0,  p.y));
    case 3: return normalize(float3( p.x, -1.0, -p.y));
    case 4: return normalize(float3( p.x, -p.y,  1.0));
    default:return normalize(float3(-p.x, -p.y, -1.0));
    }
}

float radical_inverse_vdc(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 hammersley(uint index, uint count) {
    return float2(float(index) / max(float(count), 1.0), radical_inverse_vdc(index));
}

void make_tangent_basis(float3 normal, out float3 tangent, out float3 bitangent) {
    float3 up = abs(normal.z) < 0.999
        ? float3(0.0, 0.0, 1.0)
        : float3(1.0, 0.0, 0.0);
    tangent = normalize(cross(up, normal));
    bitangent = cross(normal, tangent);
}

float3 tangent_to_world(float3 direction, float3 normal) {
    float3 tangent;
    float3 bitangent;
    make_tangent_basis(normal, tangent, bitangent);
    return normalize(
        tangent * direction.x +
        bitangent * direction.y +
        normal * direction.z
    );
}

float3 importance_sample_ggx(float2 xi, float3 normal, float perceptual_roughness) {
    float alpha = perceptual_roughness * perceptual_roughness;
    float alpha_squared = alpha * alpha;
    float phi = 2.0 * PI * xi.x;
    float cos_theta = sqrt(
        (1.0 - xi.y) /
        max(1.0 + (alpha_squared - 1.0) * xi.y, 0.000001)
    );
    float sin_theta = sqrt(max(1.0 - cos_theta * cos_theta, 0.0));
    return tangent_to_world(
        float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta),
        normal
    );
}

float3 cosine_sample_hemisphere(float2 xi, float3 normal) {
    float radius = sqrt(xi.x);
    float phi = 2.0 * PI * xi.y;
    return tangent_to_world(
        float3(radius * cos(phi), radius * sin(phi), sqrt(max(1.0 - xi.x, 0.0))),
        normal
    );
}
