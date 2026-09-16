#include "material_generator.h"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void print_usage(std::ostream &stream) {
	stream
		<< "Usage: material-generator <model.gltf|model.glb> "
		   "[output-directory] [options]\n"
		<< "\n"
		<< "Options:\n"
		<< "  -o, --output <directory>  Output directory. Defaults to the "
		   "model's directory.\n"
		<< "  --pipeline <name>         Compatible material pipeline "
		   "(default: pbr).\n"
		<< "  --asset-root <directory>  Additional root for external images; "
		   "repeatable.\n"
		<< "  --extract-textures        Copy/extract images and generate derived "
		   "maps.\n"
		<< "  --clean-source            After a successful conversion, delete the "
		   "source model, external buffers,\n"
		<< "                            and converted images beneath its input "
		   "directory. Requires --extract-textures\n"
		<< "                            and a separate output directory.\n"
		<< "  --overwrite               Replace files generated previously.\n"
		<< "  -h, --help                Show this help.\n";
}

MaterialGeneratorOptions parse_arguments(int argc, char **argv) {
	MaterialGeneratorOptions options;
	bool has_input = false;
	bool has_output = false;

	for(int i = 1; i < argc; i++) {
		const std::string_view argument(argv[i]);
		if(argument == "-h" || argument == "--help") {
			print_usage(std::cout);
			std::exit(0);
		}
		if(argument == "--overwrite") {
			options.overwrite = true;
			continue;
		}
		if(argument == "--extract-textures") {
			options.extract_textures = true;
			continue;
		}
		if(argument == "--clean-source" || argument == "--delete-source") {
			options.clean_source = true;
			continue;
		}
		if(argument == "-o" || argument == "--output") {
			if(++i >= argc)
				throw std::runtime_error(argument.data() + std::string(" requires a value"));
			options.output_directory = argv[i];
			has_output = true;
			continue;
		}
		if(argument == "--pipeline") {
			if(++i >= argc)
				throw std::runtime_error("--pipeline requires a value");
			options.pipeline = argv[i];
			if(options.pipeline.empty())
				throw std::runtime_error("--pipeline must not be empty");
			continue;
		}
		if(argument == "--asset-root") {
			if(++i >= argc)
				throw std::runtime_error("--asset-root requires a value");
			options.asset_roots.emplace_back(argv[i]);
			continue;
		}
		if(argument.starts_with('-'))
			throw std::runtime_error("unknown option: " + std::string(argument));

		if(!has_input) {
			options.input = argv[i];
			has_input = true;
		} else if(!has_output) {
			options.output_directory = argv[i];
			has_output = true;
		} else {
			throw std::runtime_error("too many positional arguments");
		}
	}

	if(!has_input)
		throw std::runtime_error("no input glTF or GLB was provided");

	if(!has_output) {
		options.output_directory = options.input.parent_path();
		if(options.output_directory.empty())
			options.output_directory = ".";
	}
	return options;
}

} // namespace

int main(int argc, char **argv) {
	try {
		if(argc == 1) {
			print_usage(std::cerr);
			return 1;
		}

		const MaterialGeneratorResult result = MaterialGenerator::generate(
			parse_arguments(argc, argv)
		);

		std::cout
			<< "Generated " << result.material_count << " material(s) and "
			<< result.texture_count << " texture(s).\n";
		if(result.copied_asset_count != 0) {
			std::cout << "Copied " << result.copied_asset_count
				<< " model asset file(s).\n";
		}
		if(result.removed_source_count != 0) {
			std::cout << "Removed " << result.removed_source_count
				<< " source asset file(s).\n";
		}
		std::cout << "Model manifest: " << result.model_manifest.string() << '\n';
		for(const auto &warning : result.warnings)
			std::cerr << "warning: " << warning << '\n';
		return 0;
	} catch(const std::exception &error) {
		std::cerr << "material-generator: " << error.what() << '\n';
		return 1;
	}
}
