#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#include "shader.h"

class VFS;

class PipelineRegistry {
	struct M {
		VFS *vfs = nullptr;
		std::map<std::string, Pipeline> pipelines;
	} m;

	static void load_pipeline(
		VFS &vfs,
		const std::filesystem::path &path,
		std::map<std::string, Pipeline> &pipelines
	);
	static std::map<std::string, Pipeline> load_all(VFS &vfs);

	explicit PipelineRegistry(M m) : m(std::move(m)) {}

public:
	PipelineRegistry(PipelineRegistry &&) noexcept = default;
	PipelineRegistry &operator=(PipelineRegistry &&) noexcept = default;
	PipelineRegistry(const PipelineRegistry &) = delete;
	PipelineRegistry &operator=(const PipelineRegistry &) = delete;

	static PipelineRegistry create(VFS &vfs);

	Pipeline *find(std::string_view name);
	Pipeline &at(std::string_view name);

	void rebuild_all();
};
