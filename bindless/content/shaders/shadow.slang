#include "shader_constants.slangh"

struct VertexOutput {
	float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertex_main(uint vertex_id: SV_VertexID, uint draw_id: SV_VulkanInstanceID) {
	VertexOutput output;

	Scene scene = get_scene();
	Object object = get_object(draw_id);

	uint index = get_index(vertex_id);
  	VertexData vertex = get_vertex(index);

  	float4 world_position = mul(object.model, vertex.position).xyzw;
  	float4x4 view_projection = mul(scene.projection, scene.view);
  	output.position = mul(view_projection, world_position);

	return output;
}

// Empty frag shader == no writes to output only depth test
[shader("fragment")]
void fragment_main(VertexOutput input) {}