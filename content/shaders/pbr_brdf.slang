struct PBRSurface
{
    float3 albedo;
    float3 normal;
    float3 emission;
    float3 f0;
    float metallic;
    float perceptual_roughness;
    float alpha_roughness;
    float ambient_occlusion;
};

struct PBRView
{
    float3 direction;
    float ndotv;
    float3 specular_energy_compensation;
};

struct StandardPBR
{
    static float pow5(float value)
    {
        float value_squared = value * value;
        return value_squared * value_squared * value;
    }

    static float distribution_ggx(float ndoth, float alpha_roughness)
    {
        float alpha_squared = alpha_roughness * alpha_roughness;
        float denominator = ndoth * ndoth * (alpha_squared - 1.0) + 1.0;
        return alpha_squared / max(PI * denominator * denominator, 0.000001);
    }

    // Height-correlated Smith visibility. This directly returns G2 / (4 NoL NoV),
    // so it can be multiplied by the normal distribution and Fresnel terms.
    static float visibility_smith_ggx_correlated(
        float ndotv,
        float ndotl,
        float alpha_roughness)
    {
        float alpha_squared = alpha_roughness * alpha_roughness;
        float view = ndotl * sqrt(
            ndotv * ndotv * (1.0 - alpha_squared) + alpha_squared);
        float light = ndotv * sqrt(
            ndotl * ndotl * (1.0 - alpha_squared) + alpha_squared);
        return 0.5 / max(view + light, 0.000001);
    }

    static float3 fresnel_schlick(float cos_theta, float3 f0)
    {
        return f0 + (1.0 - f0) * pow5(1.0 - saturate(cos_theta));
    }

    // Disney's diffuse response adds rough-surface retroreflection while
    // retaining the familiar Lambertian 1 / PI normalization.
    static float diffuse_burley(
        float ndotv,
        float ndotl,
        float ldoth,
        float perceptual_roughness)
    {
        float fd90 = 0.5
            + 2.0 * perceptual_roughness * ldoth * ldoth;
        float light_scatter = 1.0
            + (fd90 - 1.0) * pow5(1.0 - ndotl);
        float view_scatter = 1.0
            + (fd90 - 1.0) * pow5(1.0 - ndotv);
        return light_scatter * view_scatter / PI;
    }

    // Rational quadratic fit to the directional albedo of single-scattering
    // GGX with unit Fresnel. The fit is evaluated once per shaded pixel and is
    // used by Turquin's practical multiple-scattering energy compensation.
    static float ggx_directional_albedo(
        float ndotv,
        float alpha_roughness)
    {
        float x = ndotv;
        float y = alpha_roughness;
        float x_squared = x * x;
        float y_squared = y * y;

        float4 fit = float4(0.1003, 0.9345, 1.0, 1.0)
            + float4(-0.6303, -2.323, -1.765, 0.2281) * x
            + float4(9.748, 2.229, 8.263, 15.94) * y
            + float4(-2.038, -3.748, 11.53, -55.83) * x * y
            + float4(29.34, 1.424, 28.96, 13.08) * x_squared
            + float4(-8.245, -0.7684, -7.507, 41.26) * y_squared
            + float4(-26.44, 1.436, -36.11, 54.9) * x_squared * y
            + float4(19.99, 0.2913, 15.86, 300.2) * x * y_squared
            + float4(-5.448, 0.6286, 33.37, -285.1)
                * x_squared * y_squared;

        float2 response = saturate(fit.xy / fit.zw);
        return clamp(response.x + response.y, 0.0001, 1.0);
    }

    static float3 multiple_scattering_compensation(
        float ndotv,
        float alpha_roughness,
        float3 f0)
    {
        float3 average_fresnel = f0 + (1.0 - f0) * (1.0 / 21.0);
        float single_scatter_albedo = ggx_directional_albedo(
            ndotv,
            alpha_roughness);
        return 1.0 + average_fresnel
            * ((1.0 - single_scatter_albedo) / single_scatter_albedo);
    }

    static PBRView prepare_view(PBRSurface surface, float3 direction)
    {
        PBRView view;
        view.direction = normalize(direction);
        view.ndotv = saturate(dot(surface.normal, view.direction));
        view.specular_energy_compensation = multiple_scattering_compensation(
            view.ndotv,
            surface.alpha_roughness,
            surface.f0);
        return view;
    }

    static float3 evaluate_direct_light(
        PBRSurface surface,
        PBRView view,
        float3 light_direction,
        float3 radiance)
    {
        float ndotl = saturate(dot(surface.normal, light_direction));
        if(ndotl <= 0.0 || view.ndotv <= 0.0)
            return float3(0.0);

        float3 half_direction = normalize(view.direction + light_direction);
        float ndoth = saturate(dot(surface.normal, half_direction));
        float ldoth = saturate(dot(light_direction, half_direction));

        float distribution = distribution_ggx(
            ndoth,
            surface.alpha_roughness);
        float visibility = visibility_smith_ggx_correlated(
            view.ndotv,
            ndotl,
            surface.alpha_roughness);
        float3 fresnel = fresnel_schlick(ldoth, surface.f0);

        float3 specular = distribution * visibility * fresnel
            * view.specular_energy_compensation;
        float3 diffuse_weight = (1.0 - fresnel) * (1.0 - surface.metallic);
        float3 diffuse = diffuse_weight * surface.albedo * diffuse_burley(
            view.ndotv,
            ndotl,
            ldoth,
            surface.perceptual_roughness);

        return (diffuse + specular) * radiance * ndotl;
    }
};

float3 box_project_reflection(
    float3 world_position,
    float3 direction,
    float3 probe_position,
    float3 box_minimum,
    float3 box_maximum)
{
    bool inside = all(world_position >= box_minimum)
        && all(world_position <= box_maximum);
    if(!inside)
        return direction;

    float3 safe_direction = float3(
        abs(direction.x) > 0.00001 ? direction.x : (direction.x >= 0.0 ? 0.00001 : -0.00001),
        abs(direction.y) > 0.00001 ? direction.y : (direction.y >= 0.0 ? 0.00001 : -0.00001),
        abs(direction.z) > 0.00001 ? direction.z : (direction.z >= 0.0 ? 0.00001 : -0.00001)
    );
    float3 exit_plane = float3(
        safe_direction.x >= 0.0 ? box_maximum.x : box_minimum.x,
        safe_direction.y >= 0.0 ? box_maximum.y : box_minimum.y,
        safe_direction.z >= 0.0 ? box_maximum.z : box_minimum.z
    );
    float3 distances = (exit_plane - world_position) / safe_direction;
    float distance = min(distances.x, min(distances.y, distances.z));
    float3 intersection = world_position + direction * max(distance, 0.0);
    return normalize(intersection - probe_position);
}

float3 fresnel_schlick_roughness(
    float cos_theta,
    float3 f0,
    float perceptual_roughness)
{
    float3 grazing = max(float3(1.0 - perceptual_roughness), f0);
    return f0 + (grazing - f0) * StandardPBR.pow5(1.0 - cos_theta);
}

struct ProbeEnvironment
{
    float3 prefiltered;
    float3 irradiance;
};

bool reflection_probe_is_valid(ReflectionProbe probe)
{
    return descriptor_handle_is_valid(probe.specular_handle)
        && descriptor_handle_is_valid(probe.diffuse_handle)
        && probe.parameters.y > 0.0;
}

float reflection_probe_influence(ReflectionProbe probe, float3 world_position)
{
    if(any(world_position < probe.box_min.xyz)
        || any(world_position > probe.box_max.xyz))
        return 0.0;

    float3 distance_to_edge = min(
        world_position - probe.box_min.xyz,
        probe.box_max.xyz - world_position
    );
    float3 blend_distance = max(
        (probe.box_max.xyz - probe.box_min.xyz) * 0.2,
        float3(0.001)
    );
    float3 axis_weight = saturate(distance_to_edge / blend_distance);
    float edge_weight = min(axis_weight.x, min(axis_weight.y, axis_weight.z));
    return edge_weight * max(probe.parameters.z, 0.0);
}

void insert_probe_candidate(
    int index,
    float weight,
    inout int first_index,
    inout float first_weight,
    inout int second_index,
    inout float second_weight)
{
    if(weight <= 0.0)
        return;
    if(weight > first_weight) {
        second_index = first_index;
        second_weight = first_weight;
        first_index = index;
        first_weight = weight;
    } else if(weight > second_weight) {
        second_index = index;
        second_weight = weight;
    }
}

ProbeEnvironment sample_reflection_probe(
    ReflectionProbe probe,
    PBRSurface surface,
    PBRView view,
    float3 world_position)
{
    float3 reflection = reflect(-view.direction, surface.normal);
    if(probe.parameters.w < 0.5) {
        reflection = box_project_reflection(
            world_position,
            reflection,
            probe.position.xyz,
            probe.box_min.xyz,
            probe.box_max.xyz
        );
    }

    ProbeEnvironment environment;
    environment.prefiltered = nonuniform(probe.specular_handle)->SampleLevel(
        sampler_linear_clamp(),
        reflection,
        surface.perceptual_roughness * max(probe.parameters.x, 0.0)
    ).rgb * probe.parameters.y;
    environment.irradiance = nonuniform(probe.diffuse_handle)->SampleLevel(
        sampler_linear_clamp(),
        surface.normal,
        0.0
    ).rgb * probe.parameters.y;
    return environment;
}

float3 evaluate_probe_ibl(
    Scene scene,
    PBRSurface surface,
    PBRView view,
    float3 world_position)
{
    if(!descriptor_handle_is_valid(scene.reflection_probe_handle)
        || !descriptor_handle_is_valid(scene.ibl_brdf_lut_handle)
        || scene.reflection_probe_count == 0) {
        return float3(0.03) * surface.albedo * surface.ambient_occlusion;
    }

    StructuredBuffer<ReflectionProbe> probes = scene.reflection_probe_handle;
    int first_index = -1;
    int second_index = -1;
    int global_index = -1;
    float first_weight = 0.0;
    float second_weight = 0.0;
    float global_priority = -1.0;

    for(uint index = 0; index < scene.reflection_probe_count; ++index) {
        ReflectionProbe probe = probes[index];
        if(!reflection_probe_is_valid(probe))
            continue;

        if(probe.parameters.w >= 0.5) {
            if(probe.parameters.z > global_priority) {
                global_index = int(index);
                global_priority = probe.parameters.z;
            }
            continue;
        }

        insert_probe_candidate(
            int(index),
            reflection_probe_influence(probe, world_position),
            first_index,
            first_weight,
            second_index,
            second_weight
        );
    }

    // The global probe fills only the influence not covered by local probes.
    // Inserting it into the same two-entry list caps sampling at two probes.
    if(global_index >= 0) {
        insert_probe_candidate(
            global_index,
            saturate(1.0 - first_weight),
            first_index,
            first_weight,
            second_index,
            second_weight
        );
    }

    if(first_index < 0)
        return float3(0.03) * surface.albedo * surface.ambient_occlusion;

    ProbeEnvironment environment = sample_reflection_probe(
        probes[uint(first_index)],
        surface,
        view,
        world_position
    );
    float total_weight = first_weight;
    environment.prefiltered *= first_weight;
    environment.irradiance *= first_weight;

    if(second_index >= 0) {
        ProbeEnvironment second = sample_reflection_probe(
            probes[uint(second_index)],
            surface,
            view,
            world_position
        );
        environment.prefiltered += second.prefiltered * second_weight;
        environment.irradiance += second.irradiance * second_weight;
        total_weight += second_weight;
    }

    environment.prefiltered /= max(total_weight, 0.0001);
    environment.irradiance /= max(total_weight, 0.0001);

    Texture2D<float4> brdf_lut = scene.ibl_brdf_lut_handle;

    float2 integrated_brdf = brdf_lut.SampleLevel(
        sampler_linear_clamp(),
        float2(view.ndotv, surface.perceptual_roughness),
        0.0
    ).rg;

    float3 fresnel = fresnel_schlick_roughness(
        view.ndotv,
        surface.f0,
        surface.perceptual_roughness
    );
    float3 diffuse_weight = (1.0 - fresnel) * (1.0 - surface.metallic);
    float3 diffuse = diffuse_weight * surface.albedo * environment.irradiance / PI;
    float3 specular = environment.prefiltered
        * (surface.f0 * integrated_brdf.x + integrated_brdf.y)
        * view.specular_energy_compensation;

    return (diffuse + specular) * surface.ambient_occlusion;
}

extension Material
{
    PBRSurface sample_surface(
        float2 texcoord,
        float3 geometric_normal,
        float3 world_position)
    {
        PBRSurface surface;
        surface.albedo = albedo_handle.Sample(
            sampler_linear_repeat(), texcoord).rgb * base_color_factor.rgb;

        // A material without a packed map is a rough dielectric with full AO.
        // This also keeps sparse model slots from sampling an invalid handle.
        float4 mrao = descriptor_handle_is_valid(mrao_handle)
            ? mrao_handle.Sample(sampler_linear_repeat(), texcoord)
            : float4(0.0, 1.0, 1.0, 1.0);
        surface.metallic = saturate(mrao.r);
        surface.perceptual_roughness = clamp(mrao.g, 0.045, 1.0);
        surface.alpha_roughness = surface.perceptual_roughness
            * surface.perceptual_roughness;
        surface.ambient_occlusion = saturate(mrao.b);
        surface.emission = descriptor_handle_is_valid(emission_handle)
            ? emission_handle.Sample(sampler_linear_repeat(), texcoord).rgb
            : float3(0.0);

        surface.normal = normalize(geometric_normal);
        if(descriptor_handle_is_valid(normal_handle)) {
            float3 position_dx = ddx(world_position);
            float3 position_dy = ddy(world_position);
            float2 texcoord_dx = ddx(texcoord);
            float2 texcoord_dy = ddy(texcoord);

            float3 dy_perpendicular = cross(position_dy, surface.normal);
            float3 dx_perpendicular = cross(surface.normal, position_dx);
            float3 tangent = dy_perpendicular * texcoord_dx.x
                + dx_perpendicular * texcoord_dy.x;
            float3 bitangent = dy_perpendicular * texcoord_dx.y
                + dx_perpendicular * texcoord_dy.y;

            float inverse_length = rsqrt(max(
                max(dot(tangent, tangent), dot(bitangent, bitangent)),
                0.000001));
            tangent = normalize(
                tangent * inverse_length
                - surface.normal * dot(surface.normal, tangent * inverse_length));
            bitangent = normalize(cross(surface.normal, tangent));

            float3 sampled_normal = normal_handle.Sample(
                sampler_linear_repeat(), texcoord).rgb * 2.0 - 1.0;
            surface.normal = normalize(
                tangent * sampled_normal.x
                + bitangent * sampled_normal.y
                + surface.normal * sampled_normal.z);
        }

        surface.f0 = lerp(
            float3(0.04),
            saturate(surface.albedo),
            surface.metallic);
        return surface;
    }
};
