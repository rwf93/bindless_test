#include "pipeline_registry.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>
#include <tomlcpp.hpp>

#include "gfx/create_utils.h"
#include "gfx/detail/types.h"
#include "vfs.h"

namespace {

gfx::Format parse_format(const std::string &name, gfx::Format swapchain_format) {
	if(name == "SWAPCHAIN") {
		if(swapchain_format == gfx::Format::Undefined) {
			throw std::runtime_error(
				"pipeline requests SWAPCHAIN format without a presentation format"
			);
		}
		return swapchain_format;
	}

	static const std::unordered_map<std::string, gfx::Format> formats = {
		{"R8_UNORM", gfx::Format::R8Unorm},
		{"R8G8B8A8_UNORM", gfx::Format::R8G8B8A8Unorm},
		{"R8G8B8A8_SRGB", gfx::Format::R8G8B8A8Srgb},
		{"B8G8R8A8_UNORM", gfx::Format::B8G8R8A8Unorm},
		{"B8G8R8A8_SRGB", gfx::Format::B8G8R8A8Srgb},
		{"R16G16B16A16_UNORM", gfx::Format::R16G16B16A16Unorm},
		{"R16G16B16A16_SFLOAT", gfx::Format::R16G16B16A16Float},
		{"R32G32B32A32_SFLOAT", gfx::Format::R32G32B32A32Float},
		{"D16_UNORM", gfx::Format::D16Unorm},
		{"D32_SFLOAT", gfx::Format::D32Float},
		{"D24_UNORM_S8_UINT", gfx::Format::D24UnormS8Uint},
	};

	auto it = formats.find(name);
	if(it == formats.end())
		throw std::runtime_error("unknown pipeline format '" + name + "'");
	return it->second;
}

gfx::CullMode parse_cull(const std::string &name) {
	if(name == "none") return gfx::CullMode::None;
	if(name == "front") return gfx::CullMode::Front;
	if(name == "back") return gfx::CullMode::Back;
	if(name == "both") return gfx::CullMode::FrontAndBack;
	throw std::runtime_error("unknown pipeline cull mode '" + name + "'");
}

gfx::CompareOp parse_depth_compare(const std::string &name) {
	static const std::unordered_map<std::string, gfx::CompareOp> comparisons = {
		{"never", gfx::CompareOp::Never},
		{"less", gfx::CompareOp::Less},
		{"equal", gfx::CompareOp::Equal},
		{"less_or_equal", gfx::CompareOp::LessOrEqual},
		{"greater", gfx::CompareOp::Greater},
		{"not_equal", gfx::CompareOp::NotEqual},
		{"greater_or_equal", gfx::CompareOp::GreaterOrEqual},
		{"always", gfx::CompareOp::Always},
	};

	auto it = comparisons.find(name);
	if(it == comparisons.end())
		throw std::runtime_error("unknown depth comparison '" + name + "'");
	return it->second;
}

} // namespace

void PipelineRegistry::load_pipeline(
	gfx::Device &device,
	gfx::Format swapchain_format,
	VFS &vfs,
	const std::filesystem::path &path,
	PipelineMap &pipelines
) {
	auto result = toml::parseFile(path.string());
	if(!result.table)
		throw std::runtime_error("failed to parse '" + path.string() + "': " + result.errmsg);

	auto &root = *result.table;
	auto [has_name, configured_name] = root.getString("name");
	std::string name = has_name && !configured_name.empty()
		? configured_name
		: path.stem().string();

	auto [has_shader, shader_path] = root.getString("shader");
	if(!has_shader || shader_path.empty())
		throw std::runtime_error("pipeline '" + name + "' has no shader");

	auto [has_type, pipeline_type] = root.getString("type");
	if(has_type && pipeline_type == "compute") {
		const bool inserted = pipelines.try_emplace(
			name,
			with_result_of([&] {
				auto program = gfx::SlangProgram::create(
					name.c_str(),
					vfs.resolve(shader_path).string()
				);
				return PipelineRecord{
					.pipeline = gfx::Pipeline::create_compute(device, program),
					.material_layout = MaterialLayout::reflect(program),
				};
			})
		).second;
		if(!inserted)
			throw std::runtime_error("duplicate pipeline name '" + name + "'");
		spdlog::info("PipelineRegistry: loaded '{}' from '{}'", name, path.string());
		return;
	}
	if(has_type && pipeline_type != "graphics")
		throw std::runtime_error("pipeline '" + name + "' has unknown type '" + pipeline_type + "'");

	std::vector<gfx::Format> attachments;
	if(auto array = root.getArray("attachments")) {
		if(auto strings = array->getStringVector()) {
			attachments.reserve(strings->size());
			for(const auto &format : *strings)
				attachments.push_back(parse_format(format, swapchain_format));
		}
	}

	gfx::Format depth_format = gfx::Format::Undefined;
	if(auto [has_depth, depth] = root.getString("depth"); has_depth)
		depth_format = parse_format(depth, swapchain_format);

	gfx::CullMode cull = gfx::CullMode::None;
	if(auto [has_cull, value] = root.getString("cull"); has_cull)
		cull = parse_cull(value);

	bool depth_test = true;
	bool depth_write = true;
	if(auto [found, value] = root.getBool("depth_test"); found)
		depth_test = value;
	if(auto [found, value] = root.getBool("depth_write"); found)
		depth_write = value;

	gfx::CompareOp depth_compare = gfx::CompareOp::Less;
	if(auto [found, value] = root.getString("depth_compare"); found)
		depth_compare = parse_depth_compare(value);

	const bool inserted = pipelines.try_emplace(
		name,
		with_result_of([&] {
			auto program = gfx::SlangProgram::create(
				name.c_str(),
				vfs.resolve(shader_path).string()
			);
			return PipelineRecord{
				.pipeline = gfx::Pipeline::create_graphics(
					device,
					program,
					gfx::GraphicsPipelineDesc{
						.color_attachments = attachments,
						.depth_format = depth_format,
						.cull_mode = cull,
						.depth_test = depth_test,
						.depth_write = depth_write,
						.depth_compare = depth_compare,
					}
				),
				.material_layout = MaterialLayout::reflect(program),
			};
		})
	).second;
	if(!inserted)
		throw std::runtime_error("duplicate pipeline name '" + name + "'");
	spdlog::info("PipelineRegistry: loaded '{}' from '{}'", name, path.string());
}

PipelineRegistry::PipelineMap PipelineRegistry::load_all(
	gfx::Device &device,
	gfx::Format swapchain_format,
	VFS &vfs
) {
	PipelineMap pipelines;
	auto directory = vfs.resolve("pipelines");
	if(!std::filesystem::exists(directory))
		throw std::runtime_error("pipeline directory not found: " + directory.string());

	std::vector<std::filesystem::path> files;
	for(const auto &entry : std::filesystem::directory_iterator(directory)) {
		if(entry.is_regular_file() && entry.path().extension() == ".toml")
			files.push_back(entry.path());
	}
	std::sort(files.begin(), files.end());

	for(const auto &path : files) {
		try {
			load_pipeline(device, swapchain_format, vfs, path, pipelines);
		} catch(const std::exception &error) {
			spdlog::error("PipelineRegistry: {}", error.what());
		}
	}

	spdlog::info(
		"PipelineRegistry: loaded {}/{} pipeline(s) from '{}'",
		pipelines.size(),
		files.size(),
		directory.string()
	);
	return pipelines;
}

PipelineRegistry PipelineRegistry::create(
	gfx::Device &device,
	VFS &vfs,
	gfx::Format swapchain_format
) {
	return PipelineRegistry(M{
		.device = &device,
		.swapchain_format = swapchain_format,
		.vfs = &vfs,
		.pipelines = load_all(device, swapchain_format, vfs),
	});
}

gfx::Pipeline *PipelineRegistry::find(std::string_view name) {
	auto it = m.pipelines.find(std::string(name));
	return it == m.pipelines.end() ? nullptr : &it->second.pipeline;
}

gfx::Pipeline &PipelineRegistry::at(std::string_view name) {
	return m.pipelines.at(std::string(name)).pipeline;
}

const MaterialLayout *PipelineRegistry::find_material_layout(
	std::string_view name
) const {
	auto it = m.pipelines.find(std::string(name));
	return it == m.pipelines.end() ? nullptr : &it->second.material_layout;
}

void PipelineRegistry::rebuild_all() {
	m.device->wait_idle();
	PipelineMap replacements;
	try {
		replacements = load_all(*m.device, m.swapchain_format, *m.vfs);
	} catch(const std::exception &error) {
		spdlog::error(
			"PipelineRegistry: reload failed; retaining all existing pipelines: {}",
			error.what()
		);
		return;
	}

	for(const auto &[name, pipeline] : m.pipelines) {
		if(!replacements.contains(name))
			spdlog::warn("PipelineRegistry: retaining '{}' after it failed or disappeared during reload", name);
	}

	for(auto &[name, replacement] : replacements) {
		if(auto existing = m.pipelines.find(name); existing != m.pipelines.end())
			std::swap(existing->second, replacement);
	}

	// Transfer new map nodes directly; Pipeline itself is never moved into a
	// freshly emplaced node.
	m.pipelines.merge(replacements);
}
