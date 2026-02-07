#include "shader_constants.slangh"

struct VertexOutput {
  float4 position : SV_Position;
  float4 texcoord : TEXCOORD0;
  float4 normal : NORMAL0;
};

struct FragmentOutput {
  float4 color : SV_Target0;
  float4 normal : SV_Target1;
};

[shader("vertex")]
VertexOutput vertex_main(uint vertex_id: SV_VertexID,
                         uint draw_id: SV_VulkanInstanceID) {
  VertexOutput output;

  Scene scene = get_scene();
  Object object = get_object(draw_id);

  uint index = get_index(vertex_id);
  VertexData vertex = get_vertex(index);

  float4 world_position = mul(object.model, vertex.position).xyzw;
  float4x4 view_projection = mul(scene.projection, scene.view);

  output.position = mul(view_projection, world_position);
  output.texcoord = vertex.texcoord;
  output.normal = vertex.normal;

  return output;
}

[shader("fragment")]
FragmentOutput fragment_main(VertexOutput input) {
  FragmentOutput output;

  Material material = get_material();

  output.color = float4(
      get_texture_2d(material.albedo_handle).Sample(input.texcoord.xy).rgb,
      1.0);
  output.normal = input.normal;
  return output;
}
