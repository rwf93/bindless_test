#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

#include "material_generator.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fastgltf/core.hpp>
#include <fastgltf/base64.hpp>
#include <fastgltf/tools.hpp>

namespace {

template<typename... Visitors>
struct Overloaded : Visitors... {
	using Visitors::operator()...;
};

template<typename... Visitors>
Overloaded(Visitors...) -> Overloaded<Visitors...>;

struct ImageBlob {
	std::vector<std::byte> bytes;
	fastgltf::MimeType mime_type = fastgltf::MimeType::None;
};

struct DecodedImage {
	int width = 0;
	int height = 0;
	std::vector<std::uint8_t> pixels;
};

struct TextureReference {
	std::filesystem::path path;
	std::string builtin;

	static TextureReference file(std::filesystem::path path) {
		return TextureReference{.path = std::move(path)};
	}

	static TextureReference named(std::string name) {
		return TextureReference{.builtin = std::move(name)};
	}
};

struct ExternalBufferFile {
	std::filesystem::path source;
	std::filesystem::path relative_path;
};

std::string lowercase(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return value;
}

std::filesystem::path normalized_absolute(const std::filesystem::path &path) {
	std::error_code error;
	const std::filesystem::path absolute = std::filesystem::absolute(path, error);
	return (error ? path : absolute).lexically_normal();
}

std::string path_key(const std::filesystem::path &path) {
	std::string key = normalized_absolute(path).generic_string();
#ifdef _WIN32
	key = lowercase(std::move(key));
#endif
	return key;
}

bool paths_identical(
	const std::filesystem::path &left,
	const std::filesystem::path &right
) {
	std::error_code error;
	if(std::filesystem::exists(left) && std::filesystem::exists(right)) {
		const bool equivalent = std::filesystem::equivalent(left, right, error);
		if(!error && equivalent)
			return true;
	}
	return path_key(left) == path_key(right);
}

bool safe_relative_path(const std::filesystem::path &path) {
	if(path.empty() || path == "." || path.is_absolute() ||
		path.has_root_name() || path.has_root_directory())
	{
		return false;
	}
	for(const auto &component : path) {
		if(component == "..")
			return false;
	}
	return true;
}

bool path_is_within(
	const std::filesystem::path &path,
	const std::filesystem::path &directory
) {
	std::error_code path_error;
	std::error_code directory_error;
	const std::filesystem::path canonical_path = std::filesystem::weakly_canonical(
		path,
		path_error
	);
	const std::filesystem::path canonical_directory =
		std::filesystem::weakly_canonical(directory, directory_error);
	if(path_error || directory_error)
		return false;

	const std::filesystem::path relative = canonical_path.lexically_relative(
		canonical_directory
	);
	if(relative.empty())
		return canonical_path == canonical_directory;
	return safe_relative_path(relative);
}

void append_unique_path(
	std::vector<std::filesystem::path> &paths,
	const std::filesystem::path &path
) {
	const std::string key = path_key(path);
	if(std::ranges::none_of(paths, [&key](const auto &existing) {
		return path_key(existing) == key;
	})) {
		paths.push_back(normalized_absolute(path));
	}
}

std::string toml_quote(std::string_view value) {
	std::string result;
	result.reserve(value.size() + 2);
	result.push_back('"');
	for(const unsigned char character : value) {
		switch(character) {
		case '\\': result += "\\\\"; break;
		case '"': result += "\\\""; break;
		case '\b': result += "\\b"; break;
		case '\t': result += "\\t"; break;
		case '\n': result += "\\n"; break;
		case '\f': result += "\\f"; break;
		case '\r': result += "\\r"; break;
		default:
			if(character < 0x20 || character == 0x7f) {
				static constexpr char hex[] = "0123456789abcdef";
				result += "\\u00";
				result.push_back(hex[character >> 4]);
				result.push_back(hex[character & 0xf]);
			} else {
				result.push_back(static_cast<char>(character));
			}
			break;
		}
	}
	result.push_back('"');
	return result;
}

std::string format_float(float value) {
	if(!std::isfinite(value))
		throw std::runtime_error("glTF material contains a non-finite number");

	std::ostringstream stream;
	stream.imbue(std::locale::classic());
	stream << std::setprecision(9) << value;
	std::string result = stream.str();
	if(result.find_first_of(".eE") == std::string::npos)
		result += ".0";
	return result;
}

std::string toml_path(
	const std::filesystem::path &path,
	const std::filesystem::path &relative_to
) {
	std::error_code error;
	const std::filesystem::path relative = std::filesystem::relative(
		path,
		relative_to,
		error
	);
	const std::filesystem::path selected = !error && !relative.empty()
		? relative
		: path;
	std::string reference = selected.generic_string();
	// A leading ./ prevents paths such as "materials/foo.toml" from being
	// mistaken for a VFS mount when they are meant to be sidecar-relative.
	if(selected.is_relative() && !reference.starts_with('.'))
		reference = "./" + reference;
	return toml_quote(reference);
}

std::vector<std::string> material_slot_names(const fastgltf::Asset &asset) {
	std::vector<std::string> names;
	names.reserve(asset.materials.size());
	std::unordered_set<std::string> used_names;

	for(std::size_t index = 0; index < asset.materials.size(); index++) {
		std::string name(asset.materials[index].name);
		if(name.empty())
			name = "material_" + std::to_string(index);

		if(!used_names.insert(name).second) {
			name += "_" + std::to_string(index);
			while(!used_names.insert(name).second)
				name += "_";
		}
		names.push_back(std::move(name));
	}
	return names;
}

fastgltf::MimeType infer_mime_type(const std::vector<std::byte> &bytes) {
	auto byte = [&bytes](std::size_t index) {
		return std::to_integer<std::uint8_t>(bytes[index]);
	};

	if(bytes.size() >= 8 &&
		byte(0) == 0x89 && byte(1) == 'P' && byte(2) == 'N' &&
		byte(3) == 'G' && byte(4) == 0x0d && byte(5) == 0x0a &&
		byte(6) == 0x1a && byte(7) == 0x0a)
	{
		return fastgltf::MimeType::PNG;
	}
	if(bytes.size() >= 3 &&
		byte(0) == 0xff && byte(1) == 0xd8 && byte(2) == 0xff)
	{
		return fastgltf::MimeType::JPEG;
	}
	if(bytes.size() >= 12 &&
		byte(0) == 0xab && byte(1) == 'K' && byte(2) == 'T' &&
		byte(3) == 'X' && byte(4) == ' ' && byte(5) == '2')
	{
		return fastgltf::MimeType::KTX2;
	}
	if(bytes.size() >= 4 &&
		byte(0) == 'D' && byte(1) == 'D' && byte(2) == 'S' && byte(3) == ' ')
	{
		return fastgltf::MimeType::DDS;
	}
	if(bytes.size() >= 12 &&
		byte(0) == 'R' && byte(1) == 'I' && byte(2) == 'F' &&
		byte(3) == 'F' && byte(8) == 'W' && byte(9) == 'E' &&
		byte(10) == 'B' && byte(11) == 'P')
	{
		return fastgltf::MimeType::WEBP;
	}
	return fastgltf::MimeType::None;
}

std::string image_extension(fastgltf::MimeType mime_type) {
	switch(mime_type) {
	case fastgltf::MimeType::JPEG: return ".jpg";
	case fastgltf::MimeType::PNG: return ".png";
	case fastgltf::MimeType::KTX2: return ".ktx2";
	case fastgltf::MimeType::DDS: return ".dds";
	case fastgltf::MimeType::WEBP: return ".webp";
	default: return {};
	}
}

bool renderer_can_decode(fastgltf::MimeType mime_type) {
	return mime_type == fastgltf::MimeType::JPEG ||
		mime_type == fastgltf::MimeType::PNG;
}

std::vector<std::byte> read_file(
	const std::filesystem::path &path,
	std::size_t offset
) {
	std::ifstream stream(path, std::ios::binary | std::ios::ate);
	if(!stream)
		throw std::runtime_error("failed to open image: " + path.string());

	const std::streamoff end = stream.tellg();
	if(end < 0 || static_cast<std::uintmax_t>(end) < offset)
		throw std::runtime_error("invalid image byte offset: " + path.string());
	const auto size = static_cast<std::size_t>(end) - offset;
	std::vector<std::byte> bytes(size);
	stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
	if(size != 0) {
		stream.read(reinterpret_cast<char *>(bytes.data()),
			static_cast<std::streamsize>(size));
	}
	if(!stream)
		throw std::runtime_error("failed to read image: " + path.string());
	return bytes;
}

class Generator {
	const MaterialGeneratorOptions &options;
	const fastgltf::Asset &asset;
	std::filesystem::path input_directory;
	std::filesystem::path output_directory;
	std::filesystem::path materials_directory;
	std::filesystem::path textures_directory;
	std::vector<std::filesystem::path> asset_roots;
	std::vector<ExternalBufferFile> external_buffers;
	std::filesystem::path copied_model;
	std::unordered_map<std::size_t, ImageBlob> image_blobs;
	std::unordered_map<std::size_t, DecodedImage> decoded_images;
	std::unordered_map<std::size_t, std::filesystem::path> extracted_images;
	std::unordered_set<std::string> generated_textures;
	std::vector<std::filesystem::path> generated_outputs;
	std::vector<std::filesystem::path> converted_source_images;
	std::vector<std::string> warnings;
	std::size_t copied_asset_count = 0;
	std::size_t removed_source_count = 0;

	void copy_asset_file(
		const std::filesystem::path &source,
		const std::filesystem::path &destination
	) {
		if(paths_identical(source, destination)) {
			append_unique_path(generated_outputs, destination);
			return;
		}
		if(std::filesystem::exists(destination) && !options.overwrite) {
			throw std::runtime_error(
				"output already exists (use --overwrite): " +
				destination.string()
			);
		}

		std::filesystem::create_directories(destination.parent_path());
		std::error_code error;
		const bool copied = std::filesystem::copy_file(
			source,
			destination,
			options.overwrite
				? std::filesystem::copy_options::overwrite_existing
				: std::filesystem::copy_options::none,
			error
		);
		if(error || !copied) {
			throw std::runtime_error(
				"failed to copy '" + source.string() + "' to '" +
				destination.string() + "'" +
				(error ? ": " + error.message() : std::string{})
			);
		}

		std::error_code source_size_error;
		std::error_code destination_size_error;
		const auto source_size = std::filesystem::file_size(
			source,
			source_size_error
		);
		const auto destination_size = std::filesystem::file_size(
			destination,
			destination_size_error
		);
		if(source_size_error || destination_size_error ||
			source_size != destination_size)
		{
			throw std::runtime_error(
				"copied asset failed verification: " + destination.string()
			);
		}

		append_unique_path(generated_outputs, destination);
		copied_asset_count++;
	}

	void copy_model_assets() {
		std::filesystem::create_directories(output_directory);
		copied_model = output_directory / options.input.filename();
		copy_asset_file(options.input, copied_model);
		for(const auto &buffer : external_buffers) {
			copy_asset_file(
				buffer.source,
				output_directory / buffer.relative_path
			);
		}
	}

	void remember_converted_source_image(const std::filesystem::path &path) {
		append_unique_path(converted_source_images, path);
	}

	bool is_generated_output(const std::filesystem::path &path) const {
		return std::ranges::any_of(generated_outputs, [&path](const auto &output) {
			return paths_identical(path, output);
		});
	}

	void remove_source_file(const std::filesystem::path &path) {
		if(is_generated_output(path)) {
			warnings.push_back(
				"kept source asset because it is also a generated output: " +
				path.string()
			);
			return;
		}
		if(!path_is_within(path, input_directory)) {
			warnings.push_back(
				"kept source asset outside the input directory: " + path.string()
			);
			return;
		}

		std::error_code error;
		const bool removed = std::filesystem::remove(path, error);
		if(error) {
			warnings.push_back(
				"failed to remove source asset '" + path.string() + "': " +
				error.message()
			);
			return;
		}
		if(removed)
			removed_source_count++;
	}

	void clean_source_files() {
		std::vector<std::filesystem::path> sources;
		for(const auto &image : converted_source_images)
			append_unique_path(sources, image);
		for(const auto &buffer : external_buffers)
			append_unique_path(sources, buffer.source);
		append_unique_path(sources, options.input);

		for(const auto &source : sources)
			remove_source_file(source);
	}

	void write_bytes(
		const std::filesystem::path &path,
		const std::byte *data,
		std::size_t size,
		bool texture = false
	) {
		if(std::filesystem::exists(path) && !options.overwrite) {
			throw std::runtime_error(
				"output already exists (use --overwrite): " + path.string()
			);
		}
		std::filesystem::create_directories(path.parent_path());
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		if(!stream)
			throw std::runtime_error("failed to create output: " + path.string());
		if(size != 0) {
			stream.write(
				reinterpret_cast<const char *>(data),
				static_cast<std::streamsize>(size)
			);
		}
		if(!stream)
			throw std::runtime_error("failed to write output: " + path.string());
		append_unique_path(generated_outputs, path);
		if(texture)
			generated_textures.insert(path.lexically_normal().generic_string());
	}

	void write_text(const std::filesystem::path &path, const std::string &text) {
		write_bytes(
			path,
			reinterpret_cast<const std::byte *>(text.data()),
			text.size()
		);
	}

	void write_png(
		const std::filesystem::path &path,
		int width,
		int height,
		const std::vector<std::uint8_t> &pixels
	) {
		std::vector<std::byte> encoded;
		auto append = [](void *context, void *data, int size) {
			auto &bytes = *static_cast<std::vector<std::byte> *>(context);
			const std::size_t old_size = bytes.size();
			bytes.resize(old_size + static_cast<std::size_t>(size));
			std::memcpy(bytes.data() + old_size, data, static_cast<std::size_t>(size));
		};
		if(stbi_write_png_to_func(
			append,
			&encoded,
			width,
			height,
			4,
			pixels.data(),
			width * 4
		) == 0) {
			throw std::runtime_error("failed to encode PNG: " + path.string());
		}
		write_bytes(path, encoded.data(), encoded.size(), true);
	}

	std::optional<std::filesystem::path> resolve_external_image_path(
		const fastgltf::sources::URI &source
	) const {
		if(source.uri.isDataUri())
			return std::nullopt;

		const std::filesystem::path uri_path = source.uri.fspath();
		std::vector<std::filesystem::path> candidates;
		candidates.reserve(2 + asset_roots.size() * 2);
		candidates.push_back(uri_path);
		if(uri_path.is_relative())
			candidates.push_back(input_directory / uri_path);
		for(const auto &root : asset_roots) {
			if(uri_path.is_relative())
				candidates.push_back(root / uri_path);
			candidates.push_back(root / uri_path.filename());
		}

		for(const auto &candidate : candidates) {
			if(std::filesystem::is_regular_file(candidate)) {
				return std::filesystem::absolute(candidate).lexically_normal();
			}
		}
		return std::nullopt;
	}

	ImageBlob load_image_blob(std::size_t image_index) {
		if(image_index >= asset.images.size())
			throw std::runtime_error("glTF texture references an invalid image index");

		const auto &image = asset.images[image_index];
		ImageBlob blob = std::visit(Overloaded{
			[this](const fastgltf::sources::BufferView &source) {
				const auto bytes = fastgltf::DefaultBufferDataAdapter{}(
					asset,
					source.bufferViewIndex
				);
				return ImageBlob{
					.bytes = std::vector<std::byte>(
						bytes.data(),
						bytes.data() + bytes.size()
					),
					.mime_type = source.mimeType,
				};
			},
			[](const fastgltf::sources::Array &source) {
				return ImageBlob{
					.bytes = std::vector<std::byte>(
						source.bytes.data(),
						source.bytes.data() + source.bytes.size()
					),
					.mime_type = source.mimeType,
				};
			},
			[](const fastgltf::sources::Vector &source) {
				return ImageBlob{
					.bytes = source.bytes,
					.mime_type = source.mimeType,
				};
			},
			[](const fastgltf::sources::ByteView &source) {
				return ImageBlob{
					.bytes = std::vector<std::byte>(
						source.bytes.data(),
						source.bytes.data() + source.bytes.size()
					),
					.mime_type = source.mimeType,
				};
			},
			[this, image_index](const fastgltf::sources::URI &source) {
				if(source.uri.isDataUri()) {
					const std::string_view uri = source.uri.string();
					const std::size_t comma = uri.find(',');
					if(comma == std::string_view::npos ||
						uri.substr(0, comma).find(";base64") == std::string_view::npos)
					{
						throw std::runtime_error(
							"image " + std::to_string(image_index) +
							" uses a non-base64 data URI"
						);
					}
					const auto decoded = fastgltf::base64::decode(uri.substr(comma + 1));
					std::vector<std::byte> bytes(decoded.size());
					std::memcpy(bytes.data(), decoded.data(), decoded.size());
					return ImageBlob{
						.bytes = std::move(bytes),
						.mime_type = source.mimeType,
					};
				}

				const auto path = resolve_external_image_path(source);
				if(!path.has_value()) {
					throw std::runtime_error(
						"external image " + std::to_string(image_index) +
						" was not found: " + source.uri.fspath().string() +
						" (add --asset-root if the GLB was moved)"
					);
				}
				std::vector<std::byte> bytes = read_file(
					*path,
					source.fileByteOffset
				);
				remember_converted_source_image(*path);
				return ImageBlob{
					.bytes = std::move(bytes),
					.mime_type = source.mimeType,
				};
			},
			[](const auto &) -> ImageBlob {
				throw std::runtime_error("glTF image uses an unsupported data source");
			},
		}, image.data);

		if(blob.mime_type == fastgltf::MimeType::None)
			blob.mime_type = infer_mime_type(blob.bytes);
		if(blob.bytes.empty())
			throw std::runtime_error("glTF image has no data");
		return blob;
	}

	const ImageBlob &image_blob(std::size_t image_index) {
		auto found = image_blobs.find(image_index);
		if(found != image_blobs.end())
			return found->second;
		return image_blobs.emplace(image_index, load_image_blob(image_index))
			.first->second;
	}

	const DecodedImage &decode_image(std::size_t image_index) {
		auto found = decoded_images.find(image_index);
		if(found != decoded_images.end())
			return found->second;

		const ImageBlob &blob = image_blob(image_index);
		if(blob.bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
			throw std::runtime_error("encoded image is too large for stb_image");

		int width = 0;
		int height = 0;
		int channels = 0;
		stbi_uc *decoded = stbi_load_from_memory(
			reinterpret_cast<const stbi_uc *>(blob.bytes.data()),
			static_cast<int>(blob.bytes.size()),
			&width,
			&height,
			&channels,
			4
		);
		if(!decoded) {
			const char *reason = stbi_failure_reason();
			throw std::runtime_error(
				"failed to decode glTF image " + std::to_string(image_index) +
				(reason ? ": " + std::string(reason) : std::string{})
			);
		}

		DecodedImage result{
			.width = width,
			.height = height,
			.pixels = std::vector<std::uint8_t>(
				decoded,
				decoded + static_cast<std::size_t>(width) * height * 4
			),
		};
		stbi_image_free(decoded);
		return decoded_images.emplace(image_index, std::move(result)).first->second;
	}

	std::size_t texture_image_index(std::size_t texture_index) const {
		if(texture_index >= asset.textures.size())
			throw std::runtime_error("glTF material references an invalid texture index");
		const auto &texture = asset.textures[texture_index];
		if(texture.imageIndex.has_value())
			return texture.imageIndex.value();
		if(texture.webpImageIndex.has_value())
			return texture.webpImageIndex.value();
		if(texture.ddsImageIndex.has_value())
			return texture.ddsImageIndex.value();
		if(texture.basisuImageIndex.has_value())
			return texture.basisuImageIndex.value();
		throw std::runtime_error("glTF texture has no image source");
	}

	std::filesystem::path extract_image(std::size_t image_index) {
		auto found = extracted_images.find(image_index);
		if(found != extracted_images.end())
			return found->second;

		const ImageBlob &blob = image_blob(image_index);
		if(!renderer_can_decode(blob.mime_type)) {
			throw std::runtime_error(
				"image " + std::to_string(image_index) + " uses " +
				std::string(fastgltf::getMimeTypeString(blob.mime_type)) +
				", which the renderer's stb_image path cannot decode"
			);
		}
		const std::string extension = image_extension(blob.mime_type);
		if(extension.empty())
			throw std::runtime_error("could not determine a glTF image format");

		const std::filesystem::path path = textures_directory /
			("image_" + std::to_string(image_index) + extension);
		write_bytes(path, blob.bytes.data(), blob.bytes.size(), true);
		extracted_images.emplace(image_index, path);
		return path;
	}

	void warn_texture_features(
		const fastgltf::TextureInfo &texture,
		std::size_t material_index,
		std::string_view role
	) {
		if(texture.texCoordIndex != 0) {
			warnings.push_back(
				"material " + std::to_string(material_index) + " " +
				std::string(role) + " uses TEXCOORD_" +
				std::to_string(texture.texCoordIndex) +
				"; the current shader samples TEXCOORD_0"
			);
		}
		if(texture.transform) {
			warnings.push_back(
				"material " + std::to_string(material_index) + " " +
				std::string(role) +
				" has KHR_texture_transform; the current shader ignores it"
			);
		}
	}

	TextureReference source_texture_reference(
		const fastgltf::TextureInfo &texture,
		std::size_t material_index,
		std::string_view role
	) {
		warn_texture_features(texture, material_index, role);
		const std::size_t image_index = texture_image_index(texture.textureIndex);
		if(options.extract_textures)
			return TextureReference::file(extract_image(image_index));

		const auto &source = asset.images[image_index].data;
		if(const auto *uri = std::get_if<fastgltf::sources::URI>(&source);
			uri && !uri->uri.isDataUri() && uri->fileByteOffset == 0)
		{
			if(const auto path = resolve_external_image_path(*uri)) {
				const std::string extension = lowercase(path->extension().string());
				if(extension == ".png" || extension == ".jpg" || extension == ".jpeg")
					return TextureReference::file(*path);
				warnings.push_back(
					"material " + std::to_string(material_index) + " " +
					std::string(role) + " image uses an unsupported file format; "
					"using @missing"
				);
				return TextureReference::named("@missing");
			}
		}

		warnings.push_back(
			"material " + std::to_string(material_index) + " " +
			std::string(role) +
			" has no directly usable external image; using @missing "
			"(rerun with --extract-textures)"
		);
		return TextureReference::named("@missing");
	}

	std::string texture_value(
		const TextureReference &texture,
		std::string_view color_space
	) const {
		if(!texture.path.empty()) {
			return "{ path = " + toml_path(texture.path, materials_directory) +
				", color_space = " + toml_quote(color_space) + " }";
		}
		return toml_quote(texture.builtin);
	}

	static const std::uint8_t *sample_scaled(
		const DecodedImage &image,
		int x,
		int y,
		int output_width,
		int output_height
	) {
		const int source_x = std::min(
			image.width - 1,
			static_cast<int>(
				(static_cast<std::int64_t>(x) * 2 + 1) * image.width /
				(output_width * 2)
			)
		);
		const int source_y = std::min(
			image.height - 1,
			static_cast<int>(
				(static_cast<std::int64_t>(y) * 2 + 1) * image.height /
				(output_height * 2)
			)
		);
		return image.pixels.data() +
			(static_cast<std::size_t>(source_y) * image.width + source_x) * 4;
	}

	std::filesystem::path write_mrao(
		const fastgltf::Material &material,
		std::size_t material_index
	) {
		const DecodedImage *metallic_roughness = nullptr;
		const DecodedImage *occlusion = nullptr;

		if(material.pbrData.metallicRoughnessTexture.has_value()) {
			const auto &texture = material.pbrData.metallicRoughnessTexture.value();
			warn_texture_features(texture, material_index, "metallic/roughness");
			metallic_roughness = &decode_image(
				texture_image_index(texture.textureIndex)
			);
		}
		if(material.occlusionTexture.has_value()) {
			const auto &texture = material.occlusionTexture.value();
			warn_texture_features(texture, material_index, "occlusion");
			occlusion = &decode_image(texture_image_index(texture.textureIndex));
		}

		const int width = metallic_roughness
			? metallic_roughness->width
			: (occlusion ? occlusion->width : 1);
		const int height = metallic_roughness
			? metallic_roughness->height
			: (occlusion ? occlusion->height : 1);
		if(metallic_roughness && occlusion &&
			(metallic_roughness->width != occlusion->width ||
			 metallic_roughness->height != occlusion->height))
		{
			warnings.push_back(
				"material " + std::to_string(material_index) +
				" has differently sized metallic/roughness and occlusion maps; "
				"occlusion was resampled into the packed MRAO map"
			);
		}

		const float metallic_factor = static_cast<float>(
			material.pbrData.metallicFactor
		);
		const float roughness_factor = static_cast<float>(
			material.pbrData.roughnessFactor
		);
		const float occlusion_strength = material.occlusionTexture.has_value()
			? static_cast<float>(material.occlusionTexture->strength)
			: 1.0f;
		std::vector<std::uint8_t> packed(
			static_cast<std::size_t>(width) * height * 4
		);

		auto encode = [](float value) {
			return static_cast<std::uint8_t>(std::lround(
				std::clamp(value, 0.0f, 1.0f) * 255.0f
			));
		};
		for(int y = 0; y < height; y++) {
			for(int x = 0; x < width; x++) {
				const std::uint8_t *mr = metallic_roughness
					? sample_scaled(*metallic_roughness, x, y, width, height)
					: nullptr;
				const std::uint8_t *ao = occlusion
					? sample_scaled(*occlusion, x, y, width, height)
					: nullptr;
				const float metallic = (mr ? mr[2] / 255.0f : 1.0f)
					* metallic_factor;
				const float roughness = (mr ? mr[1] / 255.0f : 1.0f)
					* roughness_factor;
				const float sampled_occlusion = ao ? ao[0] / 255.0f : 1.0f;
				const float ambient_occlusion = 1.0f + occlusion_strength
					* (sampled_occlusion - 1.0f);

				const std::size_t destination =
					(static_cast<std::size_t>(y) * width + x) * 4;
				packed[destination + 0] = encode(metallic);
				packed[destination + 1] = encode(roughness);
				packed[destination + 2] = encode(ambient_occlusion);
				packed[destination + 3] = 255;
			}
		}

		const std::filesystem::path path = textures_directory /
			("mrao_" + std::to_string(material_index) + ".png");
		write_png(path, width, height, packed);
		return path;
	}

	std::optional<TextureReference> mrao_reference(
		const fastgltf::Material &material,
		std::size_t material_index
	) {
		const float metallic = static_cast<float>(material.pbrData.metallicFactor);
		const float roughness = static_cast<float>(material.pbrData.roughnessFactor);
		const bool has_metallic_roughness =
			material.pbrData.metallicRoughnessTexture.has_value();
		const bool has_effective_occlusion = material.occlusionTexture.has_value() &&
			material.occlusionTexture->strength != 0.0f;
		const bool needs_generated_map =
			has_metallic_roughness || has_effective_occlusion;

		if(!needs_generated_map && metallic == 1.0f && roughness == 1.0f)
			return TextureReference::named("@white");
		if(!needs_generated_map && metallic == 0.0f && roughness == 1.0f)
			return std::nullopt;
		if(options.extract_textures) {
			return TextureReference::file(write_mrao(material, material_index));
		}

		warnings.push_back(
			"material " + std::to_string(material_index) +
			" needs an MRAO map to preserve its glTF material values; using "
			"@missing (rerun with --extract-textures)"
		);
		return TextureReference::named("@missing");
	}

	static float linear_to_srgb(float value) {
		value = std::clamp(value, 0.0f, 1.0f);
		return value <= 0.0031308f
			? value * 12.92f
			: 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
	}

	static float srgb_to_linear(float value) {
		return value <= 0.04045f
			? value / 12.92f
			: std::pow((value + 0.055f) / 1.055f, 2.4f);
	}

	std::optional<TextureReference> emission_reference(
		const fastgltf::Material &material,
		std::size_t material_index
	) {
		std::array<float, 3> factor{
			static_cast<float>(material.emissiveFactor[0] * material.emissiveStrength),
			static_cast<float>(material.emissiveFactor[1] * material.emissiveStrength),
			static_cast<float>(material.emissiveFactor[2] * material.emissiveStrength),
		};
		if(factor[0] == 0.0f && factor[1] == 0.0f && factor[2] == 0.0f)
			return std::nullopt;

		if(material.emissiveTexture.has_value() &&
			factor[0] == 1.0f && factor[1] == 1.0f && factor[2] == 1.0f)
		{
			return source_texture_reference(
				material.emissiveTexture.value(),
				material_index,
				"emission"
			);
		}
		if(!options.extract_textures) {
			if(!material.emissiveTexture.has_value() &&
				factor[0] == 1.0f && factor[1] == 1.0f && factor[2] == 1.0f)
			{
				return TextureReference::named("@white");
			}
			warnings.push_back(
				"material " + std::to_string(material_index) +
				" needs emissive-factor baking; using @missing "
				"(rerun with --extract-textures)"
			);
			return TextureReference::named("@missing");
		}

		if(factor[0] > 1.0f || factor[1] > 1.0f || factor[2] > 1.0f) {
			warnings.push_back(
				"material " + std::to_string(material_index) +
				" has HDR emissive output; baking to PNG clamps values above 1.0"
			);
		}

		const DecodedImage *source = nullptr;
		if(material.emissiveTexture.has_value()) {
			const auto &texture = material.emissiveTexture.value();
			warn_texture_features(texture, material_index, "emission");
			source = &decode_image(texture_image_index(texture.textureIndex));
		}
		const int width = source ? source->width : 1;
		const int height = source ? source->height : 1;
		std::vector<std::uint8_t> pixels(
			static_cast<std::size_t>(width) * height * 4
		);
		for(int y = 0; y < height; y++) {
			for(int x = 0; x < width; x++) {
				const std::uint8_t *sample = source
					? sample_scaled(*source, x, y, width, height)
					: nullptr;
				const std::size_t destination =
					(static_cast<std::size_t>(y) * width + x) * 4;
				for(int channel = 0; channel < 3; channel++) {
					const float encoded = sample ? sample[channel] / 255.0f : 1.0f;
					const float linear = srgb_to_linear(encoded) * factor[channel];
					pixels[destination + channel] = static_cast<std::uint8_t>(
						std::lround(linear_to_srgb(linear) * 255.0f)
					);
				}
				pixels[destination + 3] = 255;
			}
		}

		const std::filesystem::path path = textures_directory /
			("emission_" + std::to_string(material_index) + ".png");
		write_png(path, width, height, pixels);
		return TextureReference::file(path);
	}

	std::string material_toml(
		const fastgltf::Material &material,
		std::size_t material_index
	) {
		std::ostringstream stream;
		stream.imbue(std::locale::classic());
		stream << "# Generated from glTF material " << material_index << ".\n";
		stream << "pipeline = " << toml_quote(options.pipeline) << "\n";

		TextureReference albedo = TextureReference::named("@white");
		if(material.pbrData.baseColorTexture.has_value()) {
			albedo = source_texture_reference(
				material.pbrData.baseColorTexture.value(),
				material_index,
				"base color"
			);
		}
		stream << "albedo_handle = " << texture_value(albedo, "srgb") << "\n";

		if(const auto mrao = mrao_reference(material, material_index)) {
			stream << "mrao_handle = " << texture_value(*mrao, "linear") << "\n";
		}

		if(const auto emission = emission_reference(material, material_index)) {
			stream << "emission_handle = "
				<< texture_value(*emission, "srgb") << "\n";
		}
		if(material.normalTexture.has_value()) {
			const auto &normal_texture = material.normalTexture.value();
			const TextureReference normal = source_texture_reference(
				normal_texture,
				material_index,
				"normal"
			);
			stream << "normal_handle = "
				<< texture_value(normal, "linear") << "\n";
			if(normal_texture.scale != 1.0f) {
				warnings.push_back(
					"material " + std::to_string(material_index) +
					" has a normal scale that the current shader does not expose"
				);
			}
		}

		stream << "base_color_factor = ["
			<< format_float(static_cast<float>(material.pbrData.baseColorFactor[0])) << ", "
			<< format_float(static_cast<float>(material.pbrData.baseColorFactor[1])) << ", "
			<< format_float(static_cast<float>(material.pbrData.baseColorFactor[2])) << ", "
			<< format_float(static_cast<float>(material.pbrData.baseColorFactor[3])) << "]\n";

		if(material.alphaMode != fastgltf::AlphaMode::Opaque) {
			warnings.push_back(
				"material " + std::to_string(material_index) +
				" uses glTF transparency; pipeline/cutoff state must be configured manually"
			);
		}
		if(material.doubleSided) {
			warnings.push_back(
				"material " + std::to_string(material_index) +
				" is double-sided; raster culling must be configured in its pipeline"
			);
		}
		if(material.unlit) {
			warnings.push_back(
				"material " + std::to_string(material_index) +
				" is marked unlit; select a compatible unlit pipeline manually"
			);
		}
		return stream.str();
	}

public:
	Generator(
		const MaterialGeneratorOptions &options,
		const fastgltf::Asset &asset,
		std::vector<ExternalBufferFile> external_buffers
	) :
		options(options),
		asset(asset),
		input_directory(options.input.parent_path()),
		output_directory(options.output_directory),
		materials_directory(output_directory / "materials"),
		textures_directory(output_directory / "textures"),
		asset_roots(options.asset_roots),
		external_buffers(std::move(external_buffers))
	{}

	MaterialGeneratorResult run() {
		copy_model_assets();
		std::filesystem::create_directories(materials_directory);

		const std::filesystem::path default_material = materials_directory /
			"default_material.toml";
		std::ostringstream fallback;
		fallback
			<< "# Used by primitives without a glTF material.\n"
			<< "pipeline = " << toml_quote(options.pipeline) << "\n"
			<< "albedo_handle = \"@white\"\n"
			<< "mrao_handle = \"@white\"\n"
			<< "base_color_factor = [1.0, 1.0, 1.0, 1.0]\n";
		write_text(default_material, fallback.str());

		const std::vector<std::string> slots = material_slot_names(asset);
		std::vector<std::filesystem::path> material_paths;
		material_paths.reserve(asset.materials.size());
		for(std::size_t index = 0; index < asset.materials.size(); index++) {
			const std::filesystem::path path = materials_directory /
				("material_" + std::to_string(index) + ".toml");
			write_text(
				path,
				material_toml(asset.materials[index], index)
			);
			material_paths.push_back(path);
		}

		const std::filesystem::path manifest = output_directory /
			(options.input.stem().string() + ".model.toml");
		std::ostringstream model;
		model
			<< "# Generated material sidecar for "
			<< options.input.filename().generic_string() << ".\n"
			<< "source = " << toml_path(copied_model, output_directory) << "\n"
			<< "material_roots = [\"./materials\"]\n"
			<< "default_material = "
			<< toml_path(default_material, output_directory) << "\n";
		if(!slots.empty()) {
			model << "\n[material_overrides]\n";
			for(std::size_t index = 0; index < slots.size(); index++) {
				model << toml_quote(slots[index]) << " = "
					<< toml_path(material_paths[index], output_directory) << "\n";
			}
		}
		write_text(manifest, model.str());
		if(options.clean_source)
			clean_source_files();

		return MaterialGeneratorResult{
			.model_manifest = manifest,
			.material_count = asset.materials.size(),
			.texture_count = generated_textures.size(),
			.copied_asset_count = copied_asset_count,
			.removed_source_count = removed_source_count,
			.warnings = std::move(warnings),
		};
	}
};

fastgltf::Asset load_asset(
	const std::filesystem::path &path,
	fastgltf::Options parser_options
) {
	static constexpr auto supported_extensions =
		fastgltf::Extensions::KHR_texture_transform |
		fastgltf::Extensions::KHR_texture_basisu |
		fastgltf::Extensions::MSFT_texture_dds |
		fastgltf::Extensions::KHR_mesh_quantization |
		fastgltf::Extensions::EXT_meshopt_compression |
		fastgltf::Extensions::KHR_lights_punctual |
		fastgltf::Extensions::EXT_texture_webp |
		fastgltf::Extensions::KHR_materials_specular |
		fastgltf::Extensions::KHR_materials_ior |
		fastgltf::Extensions::KHR_materials_iridescence |
		fastgltf::Extensions::KHR_materials_volume |
		fastgltf::Extensions::KHR_materials_transmission |
		fastgltf::Extensions::KHR_materials_clearcoat |
		fastgltf::Extensions::KHR_materials_emissive_strength |
		fastgltf::Extensions::KHR_materials_sheen |
		fastgltf::Extensions::KHR_materials_unlit |
		fastgltf::Extensions::KHR_materials_anisotropy |
		fastgltf::Extensions::EXT_mesh_gpu_instancing |
		fastgltf::Extensions::MSFT_packing_normalRoughnessMetallic |
		fastgltf::Extensions::MSFT_packing_occlusionRoughnessMetallic |
		fastgltf::Extensions::KHR_materials_dispersion |
		fastgltf::Extensions::KHR_materials_variants |
		fastgltf::Extensions::KHR_accessor_float64 |
		fastgltf::Extensions::KHR_draco_mesh_compression |
		fastgltf::Extensions::GODOT_single_root |
		fastgltf::Extensions::KHR_materials_diffuse_transmission;
	fastgltf::Parser parser(supported_extensions);
	auto file = fastgltf::MappedGltfFile::FromPath(path);
	if(file.error() != fastgltf::Error::None) {
		throw std::runtime_error(
			"failed to open '" + path.string() + "': " +
			std::string(fastgltf::getErrorMessage(file.error()))
		);
	}

	const std::string extension = lowercase(path.extension().string());
	auto asset = extension == ".glb"
		? parser.loadGltfBinary(file.get(), path.parent_path(), parser_options)
		: parser.loadGltf(file.get(), path.parent_path(), parser_options);
	if(asset.error() != fastgltf::Error::None) {
		throw std::runtime_error(
			"failed to parse '" + path.string() + "': " +
			std::string(fastgltf::getErrorMessage(asset.error()))
		);
	}
	return std::move(asset.get());
}

std::vector<ExternalBufferFile> collect_external_buffers(
	const fastgltf::Asset &asset,
	const std::filesystem::path &input
) {
	const std::filesystem::path input_directory = input.parent_path();
	std::vector<ExternalBufferFile> files;
	for(std::size_t index = 0; index < asset.buffers.size(); index++) {
		const auto *uri = std::get_if<fastgltf::sources::URI>(
			&asset.buffers[index].data
		);
		if(!uri || uri->uri.isDataUri())
			continue;

		const std::filesystem::path uri_path = uri->uri.fspath();
		std::filesystem::path source = uri_path.is_relative()
			? input_directory / uri_path
			: uri_path;
		source = normalized_absolute(source);

		std::error_code error;
		std::filesystem::path relative_path = uri_path.is_relative()
			? uri_path.lexically_normal()
			: std::filesystem::relative(source, input_directory, error);
		if(error || !safe_relative_path(relative_path)) {
			throw std::runtime_error(
				"external buffer " + std::to_string(index) +
				" cannot be copied without escaping the output directory: " +
				uri_path.string()
			);
		}
		relative_path = relative_path.lexically_normal();
		if(!std::filesystem::is_regular_file(source)) {
			throw std::runtime_error(
				"external buffer " + std::to_string(index) +
				" was not found: " + source.string()
			);
		}

		const auto existing = std::ranges::find_if(
			files,
			[&relative_path](const ExternalBufferFile &file) {
				return path_key(file.relative_path) == path_key(relative_path);
			}
		);
		if(existing != files.end()) {
			if(!paths_identical(existing->source, source)) {
				throw std::runtime_error(
					"multiple external buffers map to the same output path: " +
					relative_path.string()
				);
			}
			continue;
		}
		files.push_back(ExternalBufferFile{
			.source = std::move(source),
			.relative_path = std::move(relative_path),
		});
	}
	return files;
}

} // namespace

MaterialGeneratorResult MaterialGenerator::generate(
	const MaterialGeneratorOptions &raw_options
) {
	MaterialGeneratorOptions options = raw_options;
	options.input = std::filesystem::absolute(options.input).lexically_normal();
	options.output_directory = std::filesystem::absolute(
		options.output_directory
	).lexically_normal();
	for(auto &asset_root : options.asset_roots) {
		asset_root = std::filesystem::absolute(asset_root).lexically_normal();
		if(!std::filesystem::is_directory(asset_root)) {
			throw std::runtime_error(
				"asset root is not a directory: " + asset_root.string()
			);
		}
	}

	if(!std::filesystem::is_regular_file(options.input)) {
		throw std::runtime_error(
			"input does not exist or is not a file: " + options.input.string()
		);
	}
	const std::string extension = lowercase(options.input.extension().string());
	if(extension != ".glb" && extension != ".gltf") {
		throw std::runtime_error("expected a .glb or .gltf input");
	}
	if(options.pipeline.empty())
		throw std::runtime_error("pipeline must not be empty");
	if(options.clean_source && !options.extract_textures) {
		throw std::runtime_error(
			"--clean-source requires --extract-textures so material textures "
			"remain available after cleanup"
		);
	}
	if(options.clean_source && paths_identical(
		options.input,
		options.output_directory / options.input.filename()
	)) {
		throw std::runtime_error(
			"--clean-source requires an output directory separate from the "
			"input model's directory"
		);
	}

	static constexpr auto metadata_options =
		fastgltf::Options::DontRequireValidAssetMember |
		fastgltf::Options::AllowDouble;
	fastgltf::Asset metadata = load_asset(options.input, metadata_options);
	std::vector<ExternalBufferFile> external_buffers = collect_external_buffers(
		metadata,
		options.input
	);

	fastgltf::Asset asset = load_asset(
		options.input,
		metadata_options | fastgltf::Options::LoadExternalBuffers
	);
	return Generator(options, asset, std::move(external_buffers)).run();
}
