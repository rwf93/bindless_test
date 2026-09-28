#pragma once

#include <VkBootstrap.h>

#include <string>
#include <utility>

namespace gfx {

struct InstanceDesc {
	std::string application_name = "gfx application";
	std::string engine_name = "gfx";
	bool validation = false;
	bool headless = false;
};

class Instance {
	struct M {
		vkb::Instance instance;
		vkb::InstanceDispatchTable dispatch;
	} m;

	explicit Instance(M m) : m(std::move(m)) {}

public:
	~Instance();

	Instance(const Instance &) = delete;
	Instance &operator=(const Instance &) = delete;
	Instance(Instance &&) = delete;
	Instance &operator=(Instance &&) = delete;

	static Instance create(const InstanceDesc &desc = {});

	VkInstance native() const { return m.instance.instance; }
	vkb::Instance &bootstrap() { return m.instance; }
	const vkb::Instance &bootstrap() const { return m.instance; }
	vkb::InstanceDispatchTable &dispatch() { return m.dispatch; }
	const vkb::InstanceDispatchTable &dispatch() const { return m.dispatch; }
};

} // namespace gfx
