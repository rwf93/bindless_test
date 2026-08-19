#include "vfs.h"

#include <spdlog/spdlog.h>
#include <tomlcpp.hpp>

// -----------------------------------------------------------------------
// Path helpers
// -----------------------------------------------------------------------

static bool is_absolute_path(const std::string &p) {
	if(p.empty()) return false;
	if(p[0] == '/') return true;
	// Windows drive letter: "C:\..." or "C:/..."
	if(p.size() >= 3 && p[1] == ':' && (p[2] == '\\' || p[2] == '/')) return true;
	return false;
}

// Normalize a possibly-relative path against a base dir into an absolute,
// lexically-clean path. Does not touch the filesystem.
static std::filesystem::path resolve_against(
	const std::filesystem::path &base, const std::string &p)
{
	auto pp = std::filesystem::path(p);
	if(pp.is_absolute()) return pp.lexically_normal();
	return (base / pp).lexically_normal();
}

// -----------------------------------------------------------------------
// VFS::create — parse the TOML and build the mount table
// -----------------------------------------------------------------------

VFS VFS::create(const std::filesystem::path &config_path) {
	M m;
	// Default root: the directory containing vfs.toml. Resolves to absolute
	// because config_path comes in absolute from main().
	m.root = config_path.parent_path();

	auto result = toml::parseFile(config_path.string());
	if(!result.table) {
		spdlog::error("VFS::create: failed to parse '{}': {}",
			config_path.string(), result.errmsg);
		return VFS(std::move(m));
	}

	auto &root = *result.table;

	// Optional: override root. Relative values resolve against the TOML dir.
	auto [has_root, root_val] = root.getString("root");
	if(has_root && !root_val.empty()) {
		m.root = resolve_against(m.root, root_val);
	}

	// [[mount]] entries — array-of-tables with alias + path.
	if(auto arr = root.getArray("mount")) {
		if(arr->kind() == 't') {
			for(int i = 0; i < arr->size(); i++) {
				auto mt = arr->getTable(i);
				if(!mt) continue;
				auto [has_alias, alias] = mt->getString("alias");
				auto [has_path, path]   = mt->getString("path");
				if(!has_alias || alias.empty() || !has_path || path.empty()) {
					spdlog::warn("VFS::create: mount #{} missing 'alias' or 'path'; skipping", i);
					continue;
				}
				auto target = resolve_against(m.root, path);
				m.mounts[alias] = target;
				spdlog::info("VFS mount: {:<10} -> {}", alias, target.string());
			}
		} else {
			spdlog::warn("VFS::create: 'mount' must be an array-of-tables");
		}
	}

	if(m.mounts.empty())
		spdlog::warn("VFS::create: no mounts defined in '{}'", config_path.string());

	return VFS(std::move(m));
}

// -----------------------------------------------------------------------
// VFS::resolve
// -----------------------------------------------------------------------

std::filesystem::path VFS::resolve(const std::string &virtual_path) const {
	if(virtual_path.empty()) return m.root;

	// Absolute passthrough — keeps backwards-compat for callers that still
	// hand us a Windows/POSIX absolute path.
	if(is_absolute_path(virtual_path))
		return std::filesystem::path(virtual_path).lexically_normal();

	// Split on the first separator: "<alias>/<rest>".
	auto sep = virtual_path.find_first_of("/\\");
	if(sep == std::string::npos) {
		// No separator — whole input is an alias with no subpath.
		auto it = m.mounts.find(virtual_path);
		if(it != m.mounts.end()) return it->second;
		return m.root / virtual_path;
	}

	std::string alias = virtual_path.substr(0, sep);
	std::string rest  = virtual_path.substr(sep + 1);

	auto it = m.mounts.find(alias);
	if(it != m.mounts.end())
		return (it->second / rest).lexically_normal();

	// Fallback: no alias matches — treat the entire input as root-relative.
	return (m.root / virtual_path).lexically_normal();
}
