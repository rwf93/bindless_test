#pragma once

#include <cstdint>

namespace gfx {

enum class Format : uint16_t {
	Undefined,
	R8Unorm,
	R8G8B8A8Unorm,
	R8G8B8A8Srgb,
	B8G8R8A8Unorm,
	B8G8R8A8Srgb,
	R16G16B16A16Unorm,
	R16G16B16A16Float,
	R32G32B32A32Float,
	D16Unorm,
	D32Float,
	D24UnormS8Uint,
};

enum class TextureUsage : uint32_t {
	None = 0,
	Sampled = 1u << 0,
	Storage = 1u << 1,
	ColorAttachment = 1u << 2,
	DepthStencilAttachment = 1u << 3,
	TransferSource = 1u << 4,
	TransferDestination = 1u << 5,
};

constexpr TextureUsage operator|(TextureUsage left, TextureUsage right) {
	return TextureUsage(uint32_t(left) | uint32_t(right));
}

constexpr TextureUsage &operator|=(TextureUsage &left, TextureUsage right) {
	left = left | right;
	return left;
}

constexpr bool has_usage(TextureUsage value, TextureUsage flag) {
	return (uint32_t(value) & uint32_t(flag)) != 0;
}

} // namespace gfx
