#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#include "gfx/slang/shader.h"
#include "material_layout.h"

class VFS;

class PipelineRegistry {
	struct PipelineRecord {
		gfx::Pipeline pipeline;
		MaterialLayout material_layout;
	};

	using PipelineMap = std::map<std::string, PipelineRecord>;

	struct M {
		gfx::Device *device = nullptr;
		gfx::Format swapchain_format = gfx::Format::Undefined;
		VFS *vfs = nullptr;
		PipelineMap pipelines;
	} m;

	static void load_pipeline(
		gfx::Device &device,
		gfx::Format swapchain_format,
		VFS &vfs,
		const std::filesystem::path &path,
		PipelineMap &pipelines
	);
	static PipelineMap load_all(
		gfx::Device &device,
		gfx::Format swapchain_format,
		VFS &vfs
	);

	explicit PipelineRegistry(M m) : m(std::move(m)) {}

public:
	PipelineRegistry(PipelineRegistry &&) noexcept = default;
	PipelineRegistry &operator=(PipelineRegistry &&) noexcept = default;
	PipelineRegistry(const PipelineRegistry &) = delete;
	PipelineRegistry &operator=(const PipelineRegistry &) = delete;

	static PipelineRegistry create(
		gfx::Device &device,
		VFS &vfs,
		gfx::Format swapchain_format = gfx::Format::Undefined
	);
	gfx::Device &device() const { return *m.device; }

	gfx::Pipeline *find(std::string_view name);
	gfx::Pipeline &at(std::string_view name);
	const MaterialLayout *find_material_layout(std::string_view name) const;

	void rebuild_all();
};
