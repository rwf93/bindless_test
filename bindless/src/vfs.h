#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

class VFS {
	struct M {
		std::filesystem::path root;
		std::unordered_map<std::string, std::filesystem::path> mounts;
	} m;

	explicit VFS(M m) : m(std::move(m)) {}
public:
	VFS() = default;
	VFS(VFS &&) noexcept = default;
	VFS &operator=(VFS &&) noexcept = default;
	VFS(const VFS &) = delete;
	VFS &operator=(const VFS &) = delete;

	static VFS create(const std::filesystem::path &config_path);

	std::filesystem::path resolve(const std::string &virtual_path) const;

	std::string resolve_string(const std::string &virtual_path) const {
		return resolve(virtual_path).string();
	}

	const std::filesystem::path &root() const { return m.root; }
	const std::unordered_map<std::string, std::filesystem::path> &mounts() const { return m.mounts; }
};