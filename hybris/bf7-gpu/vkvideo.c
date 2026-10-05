/*
 * Что живой драйвер Vulkan говорит про декодирование видео.
 *
 * Спрашиваем не строки в файле, а сам API: версию, устройство, полный
 * список расширений устройства и семейства очередей с их флагами.
 * Декодирование в Vulkan требует VK_KHR_video_queue + VK_KHR_video_decode_h264
 * и очереди с битом VK_QUEUE_VIDEO_DECODE_BIT_KHR (0x20).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>

#define VIDEO_DECODE_BIT 0x00000020u
#define VIDEO_ENCODE_BIT 0x00000040u

int main(void)
{
	uint32_t v = 0;

	if (vkEnumerateInstanceVersion(&v) == VK_SUCCESS)
		printf("загрузчик: Vulkan %u.%u.%u\n",
		       VK_VERSION_MAJOR(v), VK_VERSION_MINOR(v), VK_VERSION_PATCH(v));

	VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
				  .pApplicationName = "bf7-vkvideo",
				  .apiVersion = VK_API_VERSION_1_1 };
	VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
				     .pApplicationInfo = &app };
	VkInstance inst;
	VkResult r = vkCreateInstance(&ici, NULL, &inst);

	if (r != VK_SUCCESS) {
		printf("vkCreateInstance -> %d\n", r);
		return 1;
	}

	uint32_t n = 0;
	vkEnumeratePhysicalDevices(inst, &n, NULL);
	printf("устройств: %u\n", n);
	if (!n)
		return 1;

	VkPhysicalDevice *pd = calloc(n, sizeof(*pd));
	vkEnumeratePhysicalDevices(inst, &n, pd);

	for (uint32_t i = 0; i < n; i++) {
		VkPhysicalDeviceProperties p;

		vkGetPhysicalDeviceProperties(pd[i], &p);
		printf("\n[%u] %s, драйвер объявляет Vulkan %u.%u.%u\n", i, p.deviceName,
		       VK_VERSION_MAJOR(p.apiVersion), VK_VERSION_MINOR(p.apiVersion),
		       VK_VERSION_PATCH(p.apiVersion));

		uint32_t qn = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(pd[i], &qn, NULL);
		VkQueueFamilyProperties *q = calloc(qn, sizeof(*q));
		vkGetPhysicalDeviceQueueFamilyProperties(pd[i], &qn, q);
		printf("  семейств очередей: %u\n", qn);
		for (uint32_t k = 0; k < qn; k++) {
			printf("    [%u] очередей %u, флаги 0x%02x:", k, q[k].queueCount,
			       q[k].queueFlags);
			if (q[k].queueFlags & VK_QUEUE_GRAPHICS_BIT) printf(" графика");
			if (q[k].queueFlags & VK_QUEUE_COMPUTE_BIT)  printf(" вычисления");
			if (q[k].queueFlags & VK_QUEUE_TRANSFER_BIT) printf(" передача");
			if (q[k].queueFlags & VIDEO_DECODE_BIT)      printf(" ДЕКОДИРОВАНИЕ");
			if (q[k].queueFlags & VIDEO_ENCODE_BIT)      printf(" КОДИРОВАНИЕ");
			printf("\n");
		}

		uint32_t en = 0;
		vkEnumerateDeviceExtensionProperties(pd[i], NULL, &en, NULL);
		VkExtensionProperties *e = calloc(en, sizeof(*e));
		vkEnumerateDeviceExtensionProperties(pd[i], NULL, &en, e);
		printf("  расширений устройства: %u\n", en);

		int video = 0;

		for (uint32_t k = 0; k < en; k++)
			if (strstr(e[k].extensionName, "video") ||
			    strstr(e[k].extensionName, "VIDEO")) {
				printf("    видео: %s\n", e[k].extensionName);
				video++;
			}
		printf("  расширений со словом video: %d\n", video);

		/* Для глаза: что вообще есть, первые сорок имён. */
		printf("  список (первые 40):\n");
		for (uint32_t k = 0; k < en && k < 40; k++)
			printf("    %s\n", e[k].extensionName);
	}
	return 0;
}
