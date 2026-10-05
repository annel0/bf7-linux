/*
 * Проверка Vulkan через libhybris: доходит ли до драйвера vulkan.mt6761.so.
 *
 * Важно для браузера: Chromium 152 допускает только реализации через ANGLE, а
 * его бэкенд GLES падает в вендорской обёртке libGLES_meow.so. Бэкенд Vulkan —
 * запасной путь, и он куда строже описан для многопоточности.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

int main(void)
{
	VkApplicationInfo app = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "bf7-vktest",
		.apiVersion = VK_API_VERSION_1_1,
	};
	VkInstanceCreateInfo ci = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app,
	};
	VkInstance inst = VK_NULL_HANDLE;
	VkResult r = vkCreateInstance(&ci, NULL, &inst);

	printf("vkCreateInstance -> %d\n", (int)r);
	if (r != VK_SUCCESS)
		return 1;

	uint32_t n = 0;

	r = vkEnumeratePhysicalDevices(inst, &n, NULL);
	printf("устройств: %u (код %d)\n", n, (int)r);
	if (r != VK_SUCCESS || n == 0)
		return 1;

	VkPhysicalDevice *devs = calloc(n, sizeof(*devs));

	vkEnumeratePhysicalDevices(inst, &n, devs);

	for (uint32_t i = 0; i < n; i++) {
		VkPhysicalDeviceProperties p;

		vkGetPhysicalDeviceProperties(devs[i], &p);
		printf("  [%u] %s\n", i, p.deviceName);
		printf("      Vulkan %u.%u.%u, драйвер 0x%x, вендор 0x%x, тип %d\n",
		       VK_VERSION_MAJOR(p.apiVersion), VK_VERSION_MINOR(p.apiVersion),
		       VK_VERSION_PATCH(p.apiVersion), p.driverVersion, p.vendorID,
		       (int)p.deviceType);

		uint32_t qn = 0;

		vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qn, NULL);
		printf("      семейств очередей: %u\n", qn);
	}

	printf("ИТОГ: Vulkan доходит до драйвера\n");
	vkDestroyInstance(inst, NULL);
	return 0;
}
