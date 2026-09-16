#include "pipeline_registry.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>
#include <tomlcpp.hpp>

#include "create_utils.h"
#include "vfs.h"
#include "vkcontext.h"

namespace {

VkFormat parse_format(const std::string &name) {
	if(name == "SWAPCHAIN")
		return vkctx.swapchain.image_format;

	static const std::unordered_map<std::string, VkFormat> formats = {
		{"R8G8B8A8_UNORM", VK_FORMAT_R8G8B8A8_UNORM},
		{"R8G8B8A8_SRGB", VK_FORMAT_R8G8B8A8_SRGB},
		{"B8G8R8A8_UNORM", VK_FORMAT_B8G8R8A8_UNORM},
		{"B8G8R8A8_SRGB", VK_FORMAT_B8G8R8A8_SRGB},
		{"R16G16B16A16_UNORM", VK_FORMAT_R16G16B16A16_UNORM},
		{"R16G16B16A16_SFLOAT", VK_FORMAT_R16G16B16A16_SFLOAT},
		{"R32G32B32A32_SFLOAT", VK_FORMAT_R32G32B32A32_SFLOAT},
		{"D16_UNORM", VK_FORMAT_D16_UNORM},
		{"D32_SFLOAT", VK_FORMAT_D32_SFLOAT},
		{"D24_UNORM_S8_UINT", VK_FORMAT_D24_UNORM_S8_UINT},
	};

	auto it = formats.find(name);
	if(it == formats.end())
		throw std::runtime_error("unknown pipeline format '" + name + "'");
	return it->second;
}

VkCullModeFlagBits parse_cull(const std::string &name) {
	if(name == "none") return VK_CULL_MODE_NONE;
	if(name == "front") return VK_CULL_MODE_FRONT_BIT;
	if(name == "back") return VK_CULL_MODE_BACK_BIT;
	if(name == "both") return VK_CULL_MODE_FRONT_AND_BACK;
	throw std::runtime_error("unknown pipeline cull mode '" + name + "'");
}

VkCompareOp parse_depth_compare(const std::string &name) {
	static const std::unordered_map<std::string, VkCompareOp> comparisons = {
		{"never", VK_COMPARE_OP_NEVER},
		{"less", VK_COMPARE_OP_LESS},
		{"equal", VK_COMPARE_OP_EQUAL},
		{"less_or_equal", VK_COMPARE_OP_LESS_OR_EQUAL},
		{"greater", VK_COMPARE_OP_GREATER},
		{"not_equal", VK_COMPARE_OP_NOT_EQUAL},
		{"greater_or_equal", VK_COMPARE_OP_GREATER_OR_EQUAL},
		{"always", VK_COMPARE_OP_ALWAYS},
	};

	auto it = comparisons.find(name);
	if(it == comparisons.end())
		throw std::runtime_error("unknown depth comparison '" + name + "'");
	return it->second;
}

} // namespace

void PipelineRegistry::load_pipeline(
	VFS &vfs,
	const std::filesystem::path &path,
	std::map<std::string, Pipeline> &pipelines
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
				auto program = SlangProgram::create(
					name.c_str(),
					vfs.resolve(shader_path).string()
				);
				return Pipeline::create_compute(program);
			})
		).second;
		if(!inserted)
			throw std::runtime_error("duplicate pipeline name '" + name + "'");
		spdlog::info("PipelineRegistry: loaded '{}' from '{}'", name, path.string());
		return;
	}
	if(has_type && pipeline_type != "graphics")
		throw std::runtime_error("pipeline '" + name + "' has unknown type '" + pipeline_type + "'");

	std::vector<VkFormat> attachments;
	if(auto array = root.getArray("attachments")) {
		if(auto strings = array->getStringVector()) {
			attachments.reserve(strings->size());
			for(const auto &format : *strings)
				attachments.push_back(parse_format(format));
		}
	}

	VkFormat depth_format = VK_FORMAT_UNDEFINED;
	if(auto [has_depth, depth] = root.getString("depth"); has_depth)
		depth_format = parse_format(depth);

	VkCullModeFlagBits cull = VK_CULL_MODE_NONE;
	if(auto [has_cull, value] = root.getString("cull"); has_cull)
		cull = parse_cull(value);

	bool depth_test = true;
	bool depth_write = true;
	if(auto [found, value] = root.getBool("depth_test"); found)
		depth_test = value;
	if(auto [found, value] = root.getBool("depth_write"); found)
		depth_write = value;

	VkCompareOp depth_compare = VK_COMPARE_OP_LESS;
	if(auto [found, value] = root.getString("depth_compare"); found)
		depth_compare = parse_depth_compare(value);

	const bool inserted = pipelines.try_emplace(
		name,
		with_result_of([&] {
			auto program = SlangProgram::create(
				name.c_str(),
				vfs.resolve(shader_path).string()
			);
			return Pipeline::create(
				program,
				attachments,
				depth_format,
				cull,
				depth_test,
				depth_write,
				depth_compare
			);
		})
	).second;
	if(!inserted)
		throw std::runtime_error("duplicate pipeline name '" + name + "'");
	spdlog::info("PipelineRegistry: loaded '{}' from '{}'", name, path.string());
}

std::map<std::string, Pipeline> PipelineRegistry::load_all(VFS &vfs) {
	std::map<std::string, Pipeline> pipelines;
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
			load_pipeline(vfs, path, pipelines);
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

PipelineRegistry PipelineRegistry::create(VFS &vfs) {
	return PipelineRegistry(M{
		.vfs = &vfs,
		.pipelines = load_all(vfs),
	});
}

Pipeline *PipelineRegistry::find(std::string_view name) {
	auto it = m.pipelines.find(std::string(name));
	return it == m.pipelines.end() ? nullptr : &it->second;
}

Pipeline &PipelineRegistry::at(std::string_view name) {
	return m.pipelines.at(std::string(name));
}

void PipelineRegistry::rebuild_all() {
	device().deviceWaitIdle();
	std::map<std::string, Pipeline> replacements;
	try {
		replacements = load_all(*m.vfs);
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
