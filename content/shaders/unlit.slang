#include "shader_constants.slang"

struct Material {
		Texture2D.Handle albedo_handle;
};

[vk::push_constant]
ConstantBuffer<ShaderConstants<Material>> constants;

struct VertexOutput {
	float4 position : SV_Position;
	float4 texcoord : TEXCOORD0;
	float4 normal : NORMAL0;
	float4 world_position : TEXCOORD1;
};

struct FragmentOutput {
	float4 color : SV_Target0;
};

[shader("vertex")]
VertexOutput vertex_main(uint vertex_id: SV_VertexID,
												 uint draw_id: SV_VulkanInstanceID) {
	VertexOutput output;

	Scene scene = constants.get_scene();
	Object object = constants.get_object(draw_id);

	uint index = constants.get_index(vertex_id);
	VertexData vertex = constants.get_vertex(index);

	float4 world_position = mul(object.model, vertex.position).xyzw;
	float4x4 view_projection = mul(scene.projection, scene.view);

	output.position = mul(view_projection, world_position);
	output.texcoord = vertex.texcoord;
	output.normal = float4(
		normalize(mul((float3x3)object.model, vertex.normal.xyz)),
		0.0
	);
	output.world_position = world_position;

	return output;
}

[shader("fragment")]
FragmentOutput fragment_main(VertexOutput input) {
	FragmentOutput output;

	Material material = constants.get_material();
	Scene scene = constants.get_scene();
	float3 normal = normalize(input.normal.xyz);
	float3 light_direction = normalize(scene.shadow_light_direction.xyz);
	float visibility = cascaded_shadow_visibility(
		scene,
		input.world_position.xyz,
		normal,
		light_direction
	);
	float3 albedo = material.albedo_handle.Sample(
		sampler_linear_repeat(),
		input.texcoord.xy
	).rgb;

	output.color = float4(albedo * lerp(0.2, 1.0, visibility), 1.0);
	return output;
}
