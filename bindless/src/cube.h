static std::vector<Vertex> CUBE_VERTEX = {
		{{-1.0f, -1.0f,  1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}}, // Bottom-left
		{{ 1.0f, -1.0f,  1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}}, // Bottom-right
		{{ 1.0f,  1.0f,  1.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}}, // Top-right
		{{-1.0f,  1.0f,  1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}}, // Top-left

		// Face 2 (Back)
		{{-1.0f, -1.0f, -1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f, 0.0f}}, // Bottom-left (uv reversed for texture orientation)
		{{ 1.0f, -1.0f, -1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f, 0.0f}}, // Bottom-right
		{{ 1.0f,  1.0f, -1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f, 0.0f}}, // Top-right
		{{-1.0f,  1.0f, -1.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f, 0.0f}}, // Top-left

		// Face 3 (Top)
		{{-1.0f,  1.0f, -1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}}, // Bottom-left (relative to top face)
		{{ 1.0f,  1.0f, -1.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}}, // Bottom-right
		{{ 1.0f,  1.0f,  1.0f, 1.0f}, {1.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}}, // Top-right
		{{-1.0f,  1.0f,  1.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}}, // Top-left

		// Face 4 (Bottom)
		{{-1.0f, -1.0f, -1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f, 0.0f}}, // Bottom-left
		{{ 1.0f, -1.0f, -1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f, 0.0f}}, // Bottom-right
		{{ 1.0f, -1.0f,  1.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f, 0.0f}}, // Top-right
		{{-1.0f, -1.0f,  1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f, 0.0f}}, // Top-left

		// Face 5 (Right)
		{{ 1.0f, -1.0f, -1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}}, // Bottom-left
		{{ 1.0f, -1.0f,  1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}}, // Bottom-right
		{{ 1.0f,  1.0f,  1.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}}, // Top-right
		{{ 1.0f,  1.0f, -1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}}, // Top-left

		// Face 6 (Left)
		{{-1.0f, -1.0f, -1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}}, // Bottom-left
		{{-1.0f, -1.0f,  1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}}, // Bottom-right
		{{-1.0f,  1.0f,  1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}}, // Top-right
		{{-1.0f,  1.0f, -1.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}}  // Top-left
};

static std::vector<uint32_t> CUBE_INDEX = {
		// Face 1 (Front)
		0, 1, 2,
		2, 3, 0,

		// Face 2 (Back)
		4, 5, 6,
		6, 7, 4,

		// Face 3 (Top)
		8, 9, 10,
		10, 11, 8,

		// Face 4 (Bottom)
		12, 13, 14,
		14, 15, 12,

		// Face 5 (Right)
		16, 17, 18,
		18, 19, 16,

		// Face 6 (Left)
		20, 21, 22,
		22, 23, 20
};