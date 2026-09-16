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
	VFS(VFS &&) noexcept = default;
	VFS &operator=(VFS &&) noexcept = default;
	VFS(const VFS &) = delete;
	VFS &operator=(const VFS &) = delete;

	static VFS create(const std::filesystem::path &config_path);

	std::filesystem::path resolve(const std::string &virtual_path) const;
};
