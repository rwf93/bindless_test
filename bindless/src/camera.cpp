#include "camera.h"

void Camera::update() {
	m.velocity = glm::vec3(0);

	if(m.keySet.contains(SDLK_W))
		m.velocity.z = -1;

	if(m.keySet.contains(SDLK_S))
		m.velocity.z = 1;

	if(m.keySet.contains(SDLK_A))
		m.velocity.x = -1;

	if(m.keySet.contains(SDLK_D))
		m.velocity.x = 1;

	m.position += glm::vec3(get_rotation_matrix() * glm::vec4(m.velocity * 0.001f, 0.f));
}

void Camera::process_event(SDL_Event *event, SDL_Window *window) {

	if(event->type == SDL_EVENT_KEY_DOWN)
		m.keySet.insert(event->key.key);

	else if(event->type == SDL_EVENT_KEY_UP)
		m.keySet.erase(event->key.key);

	if(!(event->motion.state & SDL_BUTTON_MASK(3))) { return; }
	if(event->type == SDL_EVENT_MOUSE_MOTION) {
		m.yaw += static_cast<float>(event->motion.xrel) / 200;
		m.pitch -= static_cast<float>(event->motion.yrel) / 200;
	}

	//if(event->type == SDL_EVENT_KEY_DOWN) {
	//	switch(event->key.key) {
	//	case SDLK_W: m.position += m.front * (m.speed); break;
	//	case SDLK_S: m.position -= m.front * (m.speed); break;
	//	case SDLK_D: m.position += m.right * (m.speed); break;
	//	case SDLK_A: m.position -= m.right * (m.speed); break;
	//	default: break;
	//	}
	//}
}

glm::mat4 Camera::get_view_matrix() {
	glm::mat4 camera_translation = glm::translate(glm::mat4(1.f), m.position);
    glm::mat4 camera_rotation = get_rotation_matrix();

	return glm::inverse(camera_translation * camera_rotation);
}


glm::mat4 Camera::get_rotation_matrix() {
	glm::quat pitch_rotation = glm::angleAxis(m.pitch, glm::vec3 { 1.f, 0.f, 0.f });
	glm::quat yaw_rotation = glm::angleAxis(m.yaw, glm::vec3 { 0.f, -1.f, 0.f });

	return glm::toMat4(yaw_rotation) * glm::toMat4(pitch_rotation);
}