#include "material.h"

#include <algorithm>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

#include <spdlog/spdlog.h>

Material Material::create_impl(
	gfx::UploadBatch &upload,
	gfx::Pipeline &pipeline,
	const MaterialLayout &layout,
	const MaterialParam *params,
	size_t count
) {
	if(!layout.valid())
		throw std::runtime_error("Material::create: pipeline has no Material layout");

	std::vector<uint8_t> slot(layout.stride, 0);

	// Native Slang DescriptorHandle<T> values occupy uint2 in SPIR-V. The
	// descriptor index is x; y remains zero unless combined handles are added.
	for(const auto &field : layout.fields) {
		if(field.size >= sizeof(uint32_t) &&
			field.name.size() > 7 &&
			field.name.ends_with("_handle"))
		{
			const uint32_t invalid_handle = UINT32_MAX;
			std::memcpy(
				slot.data() + field.offset,
				&invalid_handle,
				sizeof(invalid_handle)
			);
		}
	}

	for(size_t i = 0; i < count; i++) {
		const MaterialParam &parameter = params[i];
		const MaterialField *field = layout.find(parameter.name);
		if(!field) {
			spdlog::warn(
				"Material::create: shader declares no field '{}' (layout stride {}); ignoring",
				parameter.name,
				layout.stride
			);
			continue;
		}

		const size_t bytes = std::min(parameter.size, field->size);
		std::memcpy(
			slot.data() + field->offset,
			parameter.storage.data(),
			bytes
		);
	}

	return Material(M{
		.pipeline = &pipeline,
		.buffer = gfx::Buffer<uint8_t>::create(
			upload,
			std::span<const uint8_t>(slot)
		),
	});
}
