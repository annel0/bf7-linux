/*
 * Какую версию Vulkan принимает наш загрузчик.
 *
 * ANGLE в Chromium отказывает с VK_ERROR_INCOMPATIBLE_DRIVER (-9): «запрошенная
 * версия драйверу не подходит». Проверяем, что отдаёт vkEnumerateInstanceVersion
 * и с какими значениями apiVersion проходит vkCreateInstance.
 */
#include <stdio.h>
#include <vulkan/vulkan.h>

static const char *res(VkResult r)
{
	switch (r) {
	case VK_SUCCESS:			return "успех";
	case VK_ERROR_INCOMPATIBLE_DRIVER:	return "INCOMPATIBLE_DRIVER (-9)";
	case VK_ERROR_INITIALIZATION_FAILED:	return "INITIALIZATION_FAILED";
	case VK_ERROR_OUT_OF_HOST_MEMORY:	return "OUT_OF_HOST_MEMORY";
	case VK_ERROR_LAYER_NOT_PRESENT:	return "LAYER_NOT_PRESENT";
	case VK_ERROR_EXTENSION_NOT_PRESENT:	return "EXTENSION_NOT_PRESENT";
	default:				return "другое";
	}
}

int main(void)
{
	uint32_t v = 0;
	VkResult r = vkEnumerateInstanceVersion(&v);

	printf("vkEnumerateInstanceVersion -> %s, версия %u.%u.%u\n",
	       res(r), VK_VERSION_MAJOR(v), VK_VERSION_MINOR(v), VK_VERSION_PATCH(v));

	const uint32_t tries[] = {
		VK_MAKE_VERSION(1, 0, 0), VK_MAKE_VERSION(1, 1, 0),
		VK_MAKE_VERSION(1, 2, 0), VK_MAKE_VERSION(1, 3, 0),
	};

	for (unsigned i = 0; i < sizeof(tries) / sizeof(tries[0]); i++) {
		VkApplicationInfo app = {
			.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
			.apiVersion = tries[i],
		};
		VkInstanceCreateInfo ci = {
			.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			.pApplicationInfo = &app,
		};
		VkInstance inst = VK_NULL_HANDLE;
		VkResult rr = vkCreateInstance(&ci, NULL, &inst);

		printf("  apiVersion %u.%u -> %s\n",
		       VK_VERSION_MAJOR(tries[i]), VK_VERSION_MINOR(tries[i]), res(rr));
		if (rr == VK_SUCCESS)
			vkDestroyInstance(inst, NULL);
	}
	return 0;
}
