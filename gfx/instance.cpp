#include "gfx/instance.h"

#include <stdexcept>

namespace gfx {

Instance Instance::create(const InstanceDesc &desc) {
	auto builder = vkb::InstanceBuilder{};
	builder
		.set_app_name(desc.application_name.c_str())
		.set_engine_name(desc.engine_name.c_str())
		.require_api_version(VK_API_VERSION_1_3)
		.set_headless(desc.headless)
		.use_default_debug_messenger()
		.enable_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
	if(desc.validation)
		builder.request_validation_layers();

	auto result = builder.build();
	if(!result)
		throw std::runtime_error("gfx::Instance::create: " + result.error().message());
	auto instance = result.value();
	return Instance(M{
		.instance = instance,
		.dispatch = instance.make_table(),
	});
}

Instance::~Instance() {
	if(m.instance.instance != VK_NULL_HANDLE)
		vkb::destroy_instance(m.instance);
}

} // namespace gfx
