#include "gfx/texture/texture_2d_array.h"

#include "gfx/device.h"
#include "gfx/detail/types.h"
#include "gfx/texture/texture_internal.h"
#include "gfx/upload_batch.h"

namespace gfx {

Texture2DArray Texture2DArray::create(
	Device &device,
	const Texture2DArrayDesc &desc
) {
	auto upload = UploadBatch::create(device);
	auto result = create(upload, desc);
	upload.submit().wait();
	return result;
}

Texture2DArray Texture2DArray::create(
	UploadBatch &upload,
	const Texture2DArrayDesc &desc
) {
	Device &device = upload.device();
	if(desc.layer_count == 0) {
		throw std::invalid_argument(
			"Texture2DArray::create: layer_count must be greater than zero"
		);
	}

	const VkFormat format = detail::to_vk_format(desc.format);
	const VkImageLayout initial_layout = texture_detail::initial_layout_for(format);
	auto image = texture_detail::create_image(
		upload,
		ImageDesc{
			.image_type = VK_IMAGE_TYPE_2D,
			.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
			.format = format,
			.extent = {desc.width, desc.height, 1},
			.usage = detail::to_vk_image_usage(desc.usage) | VK_IMAGE_USAGE_SAMPLED_BIT,
			.layer_count = desc.layer_count,
		},
		initial_layout
	);
	const ImageRef ref = image.ref();

	texture_detail::set_image_debug_names(device, ref, desc.name);
	return Texture2DArray(M{
		.image = std::move(image),
		.initial_layout = initial_layout,
		.handle = device.register_image(ref),
		.name = desc.name,
	});
}

} // namespace gfx
