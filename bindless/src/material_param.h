#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

// A material value addressed by the matching field in the shader's Material
// struct. Values are copied inline, so initializer-list temporaries are safe.
struct MaterialParam {
	static constexpr size_t max_size = 64;

	std::string name;
	std::array<uint8_t, max_size> storage{};
	size_t size = 0;

	static MaterialParam tex(const char *name, uint32_t value) {
		MaterialParam parameter;
		parameter.name = name;
		parameter.size = sizeof(value);
		std::memcpy(parameter.storage.data(), &value, sizeof(value));
		return parameter;
	}

	static MaterialParam raw(const char *name, const void *data, size_t size) {
		if(size > max_size)
			throw std::length_error("MaterialParam::raw: value exceeds inline storage");
		if(size != 0 && data == nullptr)
			throw std::invalid_argument("MaterialParam::raw: data must not be null");

		MaterialParam parameter;
		parameter.name = name;
		parameter.size = size;
		if(size != 0)
			std::memcpy(parameter.storage.data(), data, size);
		return parameter;
	}
};
