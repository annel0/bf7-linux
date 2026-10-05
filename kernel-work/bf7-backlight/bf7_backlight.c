// SPDX-License-Identifier: GPL-2.0
/*
 * BF7: устройство подсветки поверх светодиода.
 *
 * Зачем. Панель у нас выведена светодиодом — /sys/class/leds/lcd-backlight,
 * предел 255. А всё, что умеет управлять яркостью в обычном Linux (gsd-power, а
 * через него ползунок в телефонной оболочке), ищет устройство класса backlight
 * в /sys/class/backlight. Его нет: в ядре не задан CONFIG_BACKLIGHT_LCD_SUPPORT.
 *
 * Пересобирать и перепрошивать ядро ради этого не нужно. Сам класс собирается
 * модулем (backlight.ko), а этот модуль объявляет поверх него устройство и
 * передаёт яркость светодиоду.
 *
 * Как передаёт. Прямого способа дотянуться до чужого светодиода из модуля в
 * 4.19 нет: функции mt_leds_brightness_set в нашем ядре не оказалось (она есть
 * в leds-mtk-disp.c, но собран другой драйвер — leds-mt65xx). Зато есть
 * триггеры: регистрируем свой, а светодиод привязывается к нему из
 * пространства пользователя одной строкой
 *
 *     echo bf7-backlight > /sys/class/leds/lcd-backlight/trigger
 *
 * После этого led_trigger_event() меняет яркость светодиода.
 *
 * Собирается против дерева ядра как обычный внешний модуль; порядок загрузки:
 * сперва backlight.ko, потом этот.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/backlight.h>
#include <linux/leds.h>
#include <linux/fb.h>

#define BF7_BL_MAX	255
#define BF7_BL_DEFAULT	140

static struct led_trigger *bf7_trig;
static struct backlight_device *bf7_bl;
static struct platform_device *bf7_pdev;

/*
 * Горит ли подсветка сейчас — для события udev при переключении (см. ниже).
 * -1: ещё не сообщали.
 */
static int bf7_bl_gorit = -1;

/*
 * Событие «change» с BF7_PODSVETKA=0/1 при каждом включении и гашении.
 * Правило 79-bf7-podsvetka.rules по нему переключает вывод ШИМ подсветки
 * (GPIO43): при погашенной — обычный GPIO с «0», при горящей — функция
 * DISP_PWM. Иначе после глубокого сна, пока экран погашен, дисплейный блок
 * обесточен, а вывод в режиме ШИМ — и микросхема подсветки светит на полную
 * (2026-09-24: ~377 мА, экран светится пустым светом).
 */
static void bf7_bl_soobshchit(struct backlight_device *bd, int gorit)
{
	char var[20];
	char *envp[] = { var, NULL };

	if (gorit == bf7_bl_gorit)
		return;
	bf7_bl_gorit = gorit;
	snprintf(var, sizeof(var), "BF7_PODSVETKA=%d", gorit);
	kobject_uevent_env(&bd->dev.kobj, KOBJ_CHANGE, envp);
}

static int bf7_bl_update(struct backlight_device *bd)
{
	int level = bd->props.brightness;

	/*
	 * Гашение приходит отдельными полями, и уважать их обязательно: без
	 * этого экран не потухнет ни по бездействию, ни по кнопке.
	 */
	if (bd->props.power != FB_BLANK_UNBLANK ||
	    bd->props.state & (BL_CORE_SUSPENDED | BL_CORE_FBBLANK))
		level = 0;

	if (level < 0)
		level = 0;
	if (level > BF7_BL_MAX)
		level = BF7_BL_MAX;

	led_trigger_event(bf7_trig, level);
	bf7_bl_soobshchit(bd, level > 0);
	return 0;
}

static int bf7_bl_get(struct backlight_device *bd)
{
	return bd->props.brightness;
}

static const struct backlight_ops bf7_bl_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.update_status	= bf7_bl_update,
	.get_brightness	= bf7_bl_get,
};

static int __init bf7_bl_init(void)
{
	struct backlight_properties props;
	int ret;

	led_trigger_register_simple("bf7-backlight", &bf7_trig);
	if (!bf7_trig) {
		pr_err("bf7-backlight: не удалось зарегистрировать триггер\n");
		return -ENODEV;
	}

	/*
	 * Устройству подсветки нужен родитель: gsd-power отбрасывает те, у
	 * которых его нет, а заодно по нему ищется тип устройства.
	 */
	bf7_pdev = platform_device_register_simple("bf7-backlight", -1, NULL, 0);
	if (IS_ERR(bf7_pdev)) {
		ret = PTR_ERR(bf7_pdev);
		goto err_trig;
	}

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = BF7_BL_MAX;
	props.brightness = BF7_BL_DEFAULT;
	props.power = FB_BLANK_UNBLANK;

	bf7_bl = backlight_device_register("bf7-backlight", &bf7_pdev->dev,
					   NULL, &bf7_bl_ops, &props);
	if (IS_ERR(bf7_bl)) {
		ret = PTR_ERR(bf7_bl);
		pr_err("bf7-backlight: не удалось объявить устройство: %d\n", ret);
		goto err_pdev;
	}

	backlight_update_status(bf7_bl);
	pr_info("bf7-backlight: устройство подсветки готово, предел %d\n",
		BF7_BL_MAX);
	pr_info("bf7-backlight: привяжите светодиод: "
		"echo bf7-backlight > /sys/class/leds/lcd-backlight/trigger\n");
	return 0;

err_pdev:
	platform_device_unregister(bf7_pdev);
err_trig:
	led_trigger_unregister_simple(bf7_trig);
	return ret;
}

static void __exit bf7_bl_exit(void)
{
	if (bf7_bl)
		backlight_device_unregister(bf7_bl);
	if (!IS_ERR_OR_NULL(bf7_pdev))
		platform_device_unregister(bf7_pdev);
	if (bf7_trig)
		led_trigger_unregister_simple(bf7_trig);
}

module_init(bf7_bl_init);
module_exit(bf7_bl_exit);

MODULE_DESCRIPTION("BF7: устройство подсветки поверх светодиода lcd-backlight");
MODULE_LICENSE("GPL v2");
