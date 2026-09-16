#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

struct MaterialGeneratorOptions {
	std::filesystem::path input;
	std::filesystem::path output_directory;
	std::vector<std::filesystem::path> asset_roots;
	std::string pipeline = "pbr";
	bool extract_textures = false;
	bool clean_source = false;
	bool overwrite = false;
};

struct MaterialGeneratorResult {
	std::filesystem::path model_manifest;
	std::size_t material_count = 0;
	std::size_t texture_count = 0;
	std::size_t copied_asset_count = 0;
	std::size_t removed_source_count = 0;
	std::vector<std::string> warnings;
};

class MaterialGenerator {
public:
	static MaterialGeneratorResult generate(
		const MaterialGeneratorOptions &options
	);
};
