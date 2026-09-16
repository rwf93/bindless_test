float finite_light_attenuation(
    Light light,
    float3 surface_to_light,
    float distance_squared
) {
    float attenuation = rcp(distance_squared);

    if(light.range > 0.0) {
        float normalized_distance_squared = distance_squared
            / max(light.range * light.range, 0.0001);
        float range_falloff = saturate(
            1.0 - normalized_distance_squared * normalized_distance_squared);
        attenuation *= range_falloff * range_falloff;
    }

    if(light.type == LIGHT_TYPE_SPOT) {
        float3 spot_direction = normalize(light.direction.xyz);
        float cos_angle = dot(-surface_to_light, spot_direction);
        float cone = saturate(
            (cos_angle - light.outer_cone_cos)
            / max(light.inner_cone_cos - light.outer_cone_cos, 0.0001));
        cone = cone * cone * (3.0 - 2.0 * cone);
        attenuation *= cone;
    }

    return attenuation;
}

float3 evaluate_scene_direct_lighting(
    ShaderConstants<Material> constants,
    Scene scene,
    PBRSurface surface,
    PBRView view,
    float3 world_position
) {
    float3 lighting = float3(0.0);

    for(uint i = 0; i < scene.light_count; ++i) {
        Light light = constants.get_light(i);
        float3 delta = light.position.xyz - world_position;
        float distance_squared = max(dot(delta, delta), 0.0001);

        // Reject lights before the shadow-map lookup. A shadow sample cannot
        // contribute when the surface is outside the light or faces away.
        if(light.range > 0.0 && distance_squared >= light.range * light.range)
            continue;

        float3 light_direction = delta * rsqrt(distance_squared);
        if(dot(surface.normal, light_direction) <= 0.0)
            continue;

        float attenuation = finite_light_attenuation(
            light,
            light_direction,
            distance_squared
        );
        if(attenuation <= 0.0)
            continue;

        float visibility = local_light_shadow_visibility(
            scene,
            light,
            i,
            world_position,
            surface.normal,
            light_direction
        );
        lighting += StandardPBR.evaluate_direct_light(
            surface,
            view,
            light_direction,
            light.color.rgb * attenuation
        ) * visibility;
    }

    float3 sun_direction = normalize(scene.shadow_light_direction.xyz);
    if(dot(surface.normal, sun_direction) > 0.0) {
        float sun_visibility = cascaded_shadow_visibility(
            scene,
            world_position,
            surface.normal,
            sun_direction
        );
        lighting += StandardPBR.evaluate_direct_light(
            surface,
            view,
            sun_direction,
            scene.shadow_light_color.rgb
        ) * sun_visibility;
    }
    return lighting;
}
