#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "gfx/image.h"

namespace gfx {

enum class LoadOp : uint8_t {
	Load,
	Clear,
	Discard,
};

enum class StoreOp : uint8_t {
	Store,
	Discard,
};

struct ClearValue {
	std::array<float, 4> color = {0.0f, 0.0f, 0.0f, 0.0f};
	float depth = 1.0f;
	uint32_t stencil = 0;
};

struct RenderingAttachment {
	ImageRef image;
	LoadOp load = LoadOp::Load;
	StoreOp store = StoreOp::Store;
	ClearValue clear;
};

struct RenderingDesc {
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t layer_count = 1;
	std::span<const RenderingAttachment> colors;
	const RenderingAttachment *depth = nullptr;
};

} // namespace gfx
