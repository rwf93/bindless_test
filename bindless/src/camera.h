#pragma once

#include <SDL3/SDL_events.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <set>

class Camera {
	struct M {
		glm::vec3 velocity;
		glm::vec3 position;
		float yaw = 0.0f;
		float pitch = 0.0f;


		std::set<SDL_Keycode> keySet;
	} m;

	explicit Camera(M m) : m(std::move(m)) {}
public:
	static Camera create() {
		return Camera(M{
		});
	}

	void process_event(SDL_Event *event, SDL_Window *window);
	void update();

	glm::vec3 get_position() { return m.position; }
	glm::mat4 get_view_matrix();
	glm::mat4 get_rotation_matrix();
};