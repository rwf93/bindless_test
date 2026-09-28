#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace gfx {
class SlangProgram;
}

struct MaterialField {
	std::string name;
	uint32_t offset;
	size_t size;
};

struct MaterialLayout {
	size_t stride = 0;
	std::vector<MaterialField> fields;
	std::unordered_map<std::string, size_t> by_name;

	bool valid() const { return stride > 0; }

	const MaterialField *find(const std::string &name) const {
		auto it = by_name.find(name);
		return it == by_name.end() ? nullptr : &fields[it->second];
	}

	static MaterialLayout reflect(
		gfx::SlangProgram &program,
		const char *struct_name = "Material"
	);
};
