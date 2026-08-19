#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <spdlog/spdlog.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>

#include "resource.h"
#include "vktools.h"

glm::mat4 calculate_model_matrix(glm::vec3 translation, glm::vec3 rotation, glm::vec3 scale) {
	glm::mat4 translation_matrix = glm::translate(glm::mat4(1.0f), translation);
	glm::mat4 rotation_matrix = glm::mat4(glm::quat(rotation));
	glm::mat4 scale_matrix = glm::scale(glm::mat4(1.0f), scale);

	return translation_matrix * rotation_matrix * scale_matrix;
}

void Texture2D::transfer_data(
	uint32_t width,
	uint32_t height,
	AllocatedImage *image,
	void *data,
	VkImageLayout final_layout
) {
	auto staging_allocate_info = info::allocation_create_info();
	auto staging_buffer_info = info::buffer_create_info(1 * width * height * 4);

	VmaAllocation staging_allocation;
	VkBuffer staging_buffer;
	VK_CHECK(vmaCreateBuffer(vkctx.allocator, &staging_buffer_info, &staging_allocate_info, &staging_buffer, &staging_allocation, nullptr));

	void *mapped;
	vmaMapMemory(vkctx.allocator, staging_allocation, &mapped);
	memcpy(mapped, data, 1 * width * height * 4);
	vmaUnmapMemory(vkctx.allocator, staging_allocation);

	submit_command([&](VkCommandBuffer command) {
		VkBufferImageCopy copy = {};
		copy.bufferOffset = 0;
		copy.bufferRowLength = 0;
		copy.bufferImageHeight = 0;
		copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy.imageSubresource.mipLevel = 0;
		copy.imageSubresource.baseArrayLayer = 0;
		copy.imageSubresource.layerCount = 1;
		copy.imageExtent = VkExtent3D{width, height, 1};

		device().cmdCopyBufferToImage(command, staging_buffer, image->image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
	});

	vmaDestroyBuffer(vkctx.allocator, staging_buffer, staging_allocation);
}

AllocatedImage Texture2D::create_image(
	uint32_t width,
	uint32_t height,
	VkFormat format,
	VkImageUsageFlags usage
) {
	VkImageCreateInfo info = info::image_create_info(width, height, 1);
	info.imageType = VK_IMAGE_TYPE_2D;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.format = format;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;

	info.usage = usage;

	auto allocation_create_info = info::allocation_create_info(0);
	allocation_create_info.usage = VMA_MEMORY_USAGE_AUTO;

	VkImage image;
	VmaAllocation allocation;
	VmaAllocationInfo allocation_info;

	VkImageViewCreateInfo image_view_create = {};

	VK_CHECK(vmaCreateImage(
		vkctx.allocator,
		&info,
		&allocation_create_info,
		&image,
		&allocation,
		&allocation_info
	));

	VkImageView view;
	image_view_create.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	image_view_create.viewType = VK_IMAGE_VIEW_TYPE_2D;
	image_view_create.format = format;
	image_view_create.subresourceRange.baseMipLevel = 0;
	image_view_create.subresourceRange.baseArrayLayer = 0;
	image_view_create.subresourceRange.layerCount = 1;
	image_view_create.subresourceRange.levelCount = 1;
	image_view_create.subresourceRange.aspectMask = format == VK_FORMAT_D32_SFLOAT ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	image_view_create.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	image_view_create.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	image_view_create.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	image_view_create.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	image_view_create.image = image;

	VK_CHECK(device().createImageView(&image_view_create, nullptr, &view));

	submit_command([&](VkCommandBuffer command) {
		transition(command, image, VK_IMAGE_LAYOUT_UNDEFINED, format == VK_FORMAT_D32_SFLOAT ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL);
	});

	return AllocatedImage{
		.image = image,
		.view = view,
		.allocation = allocation
	};

}

Texture2D Texture2D::create(uint32_t width, uint32_t height, VkFormat format, std::function<uint32_t(int x, int y)> &&fn) {
	std::vector<uint32_t> array(width * height);
	std::generate(array.begin(), array.end(),
		[&, x = 0, y = 0]() mutable {
			uint32_t value = fn(x, y);
			if (++x == width) { x = 0; ++y; }
			return value;
	});
	return Texture2D::create(width, height, format, array.data());
}

Texture2D Texture2D::create(uint32_t width, uint32_t height, AllocatedImage image) {
	VkDescriptorImageInfo desc_image = {};
	desc_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	desc_image.imageView = image.view;
	desc_image.sampler = vkctx.sampler_address_repeat;

	VkWriteDescriptorSet write_desc = {};
	write_desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write_desc.dstBinding = 0;
	write_desc.dstArrayElement = bindless_texture_index;
	write_desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write_desc.descriptorCount = 1;
	write_desc.pImageInfo = &desc_image;

	update_descriptor_all_frames(write_desc, bindless_texture_desc);

	return Texture2D(M{
		.image = image,
		.handle = bindless_texture_index++,
		.width = width,
		.height = height
	});
}

Texture2D Texture2D::create_empty(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlagBits usage) {
	auto image_res = create_image(
		width,
		height,
		format,
		usage | VK_IMAGE_USAGE_SAMPLED_BIT
	);

	VkDescriptorImageInfo desc_image = {};
	desc_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	desc_image.imageView = image_res.view;
	desc_image.sampler = vkctx.sampler_address_repeat;

	VkWriteDescriptorSet write_desc = {};
	write_desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write_desc.dstBinding = 0;
	write_desc.dstArrayElement = bindless_texture_index;
	write_desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write_desc.descriptorCount = 1;
	write_desc.pImageInfo = &desc_image;

	update_descriptor_all_frames(write_desc, bindless_texture_desc);

	return Texture2D(M{
		.image = image_res,
		.handle = bindless_texture_index++,
		.width = width,
		.height = height
	});
}

Texture2D Texture2D::create(uint32_t width, uint32_t height, VkFormat format, void *data) {
	auto image_res = create_image(
		width,
		height,
		format,
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
	);
	transfer_data(width, height, &image_res, data, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	VkDescriptorImageInfo desc_image = {};
	desc_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	desc_image.imageView = image_res.view;
	desc_image.sampler = vkctx.sampler_address_repeat;

	VkWriteDescriptorSet write_desc = {};
	write_desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write_desc.dstBinding = 0;
	write_desc.dstArrayElement = bindless_texture_index;
	write_desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write_desc.descriptorCount = 1;
	write_desc.pImageInfo = &desc_image;

	update_descriptor_all_frames(write_desc, bindless_texture_desc);

	return Texture2D(M{
		.image = image_res,
		.handle = bindless_texture_index++,
		.width = width,
		.height = height
	});
}

Texture2D Texture2D::load_file(const std::filesystem::path &path) {
	int w = 0, h = 0, comp = 0;
	unsigned char *data = stbi_load(path.string().c_str(), &w, &h, &comp, 4);
	if(!data) {
		spdlog::error("Texture2D::load_file: failed to load '{}'", path.string());
		M empty{};
		empty.handle = UINT32_MAX;
		return Texture2D(std::move(empty));
	}
	auto tex = Texture2D::create(uint32_t(w), uint32_t(h), VK_FORMAT_R8G8B8A8_UNORM, data);
	stbi_image_free(data);
	return tex;
}

uint32_t Texture2D::handle() {
	return m.handle;
}

AllocatedImage Texture2D::image() {
	return m.image;
}

// Path-keyed cache of loaded textures, so multiple materials referencing
// the same file share one GPU Texture2D. Mirrors Material::material_cache.
std::unordered_map<std::string, Texture2D> Texture2D::cache;

uint32_t Texture2D::load_cached(const std::filesystem::path &path) {
	auto key = path.string();
	auto it = cache.find(key);
	if(it != cache.end())
		return it->second.handle();
	auto tex = Texture2D::load_file(path);
	uint32_t handle = tex.handle();
	cache.emplace(std::move(key), std::move(tex));
	return handle;
}