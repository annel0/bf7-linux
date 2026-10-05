// SPDX-License-Identifier: GPL-2.0
/*
 * BF7: фонарик — светодиод вспышки как устройство класса leds «white:torch».
 *
 * Зачем свой драйвер. В переписи железа фонарик числился за MT6370, и
 * драйвер MT6370 в ядре даже собран. Но этого чипа на плате нет: его probe
 * по адресу 5-0034 кончается -121 (никто не отвечает), дерево устройства
 * описывает соседний вариант платы. Настоящий драйвер вспышки сидит на
 * i2c-5 по адресу 0x63 (узел "mediatek,strobe_main"), а в дереве для него
 * два взаимозаменяемых узла — flashlights_aw36515 и flashlights_ocp81375 —
 * с одной и той же ножкой включения HWEN. Драйверов ни для того, ни для
 * другого в нашем ядре нет.
 *
 * Что это за чип. Регистры после сброса в точности совпадают с TI LM3644 и
 * AW3644 (включение 0x80, вспышка 0xBF/0x3F, фонарь 0xBF/0x3F, 0x09, 0x1A,
 * 0x08), отличается лишь номер устройства в 0x0C — 0x3A. Значит, клон той
 * же линейки, скорее всего AW36515. Смысл регистров сверен с драйвером
 * flashlights-aw3644.c из нашего дерева: 0x01 = 0x09 — фонарь на первом
 * светодиоде, 0x00 — выключено, 0x05 — ток фонаря первого светодиода.
 *
 * Почему класс leds, а не вендорский /dev/flashlight. Phosh ищет фонарик
 * именно так: устройство подсистемы leds с именем «*:torch» или «*:flash»
 * (src/torch-manager.c), и включает его через logind. Любой другой
 * интерфейс оболочке не виден.
 *
 * Проверено до драйвера, руками через /dev/i2c-5: при 0x01 = 0x09 флаги
 * неисправностей (0x0A, 0x0B) нулевые, а камера, смотрящая на телефон в
 * тёмной комнате, показала среднюю яркость кадра 0,3 → 129,6 → 0,2.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>

#define BF7_TORCH_REG_ENABLE	0x01
#define BF7_TORCH_REG_TORCH1	0x05
#define BF7_TORCH_REG_FLAG1	0x0a
#define BF7_TORCH_REG_FLAG2	0x0b
#define BF7_TORCH_REG_ID	0x0c

#define BF7_TORCH_ENABLE_OFF	0x00
#define BF7_TORCH_ENABLE_LED1	0x09	/* режим «фонарь», первый светодиод */

/*
 * Ступени тока фонаря — код в младших семи разрядах регистра 0x05. Верхняя
 * ступень — значение самого чипа после сброса (0x3F): это ток, который
 * производитель считает разумным по умолчанию, выше не поднимаемся. Старший
 * разряд оставляем как у чипа (единица): для одного светодиода он ни на что
 * не влияет.
 */
static const u8 bf7_torch_kody[] = { 0x0c, 0x19, 0x26, 0x32, 0x3f };

struct bf7_torch {
	struct led_classdev led;
	struct i2c_client *chip;
	struct pinctrl *pins;
	struct pinctrl_state *hwen_high;
	struct pinctrl_state *hwen_low;
	struct mutex zamok;
	bool vklyuchen;
};

static int bf7_torch_pishi(struct bf7_torch *t, u8 reg, u8 val)
{
	int ret = i2c_smbus_write_byte_data(t->chip, reg, val);

	if (ret < 0)
		dev_err(t->led.dev, "запись 0x%02x в 0x%02x не удалась: %d\n",
			val, reg, ret);
	return ret;
}

static int bf7_torch_set(struct led_classdev *led, enum led_brightness value)
{
	struct bf7_torch *t = container_of(led, struct bf7_torch, led);
	int ret = 0;

	mutex_lock(&t->zamok);

	if (value == LED_OFF) {
		if (t->vklyuchen) {
			ret = bf7_torch_pishi(t, BF7_TORCH_REG_ENABLE,
					      BF7_TORCH_ENABLE_OFF);
			pinctrl_select_state(t->pins, t->hwen_low);
			t->vklyuchen = false;
		}
		goto out;
	}

	if (value > ARRAY_SIZE(bf7_torch_kody))
		value = ARRAY_SIZE(bf7_torch_kody);

	if (!t->vklyuchen) {
		pinctrl_select_state(t->pins, t->hwen_high);
		/* Чипу нужно мгновение после подъёма HWEN, прежде чем слушать шину. */
		usleep_range(1000, 2000);
	}

	ret = bf7_torch_pishi(t, BF7_TORCH_REG_TORCH1,
			      0x80 | bf7_torch_kody[value - 1]);
	if (ret >= 0)
		ret = bf7_torch_pishi(t, BF7_TORCH_REG_ENABLE,
				      BF7_TORCH_ENABLE_LED1);

	if (ret >= 0) {
		t->vklyuchen = true;
	} else {
		/* Не оставлять чип включённым вполовину. */
		pinctrl_select_state(t->pins, t->hwen_low);
		t->vklyuchen = false;
	}

out:
	mutex_unlock(&t->zamok);
	return ret < 0 ? ret : 0;
}

static int bf7_torch_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *uzel;
	struct bf7_torch *t;
	int id, ret;

	t = devm_kzalloc(dev, sizeof(*t), GFP_KERNEL);
	if (!t)
		return -ENOMEM;
	mutex_init(&t->zamok);

	/* Ножка HWEN — из состояний этого же узла дерева. */
	t->pins = devm_pinctrl_get(dev);
	if (IS_ERR(t->pins))
		return PTR_ERR(t->pins);
	t->hwen_high = pinctrl_lookup_state(t->pins, "aw36515hwen_high");
	t->hwen_low = pinctrl_lookup_state(t->pins, "aw36515hwen_low");
	if (IS_ERR(t->hwen_high) || IS_ERR(t->hwen_low)) {
		dev_err(dev, "нет состояний aw36515hwen_high/low в дереве\n");
		return -ENODEV;
	}

	/* Сам чип — клиент i2c узла strobe_main, к которому драйвер не привязан. */
	uzel = of_find_compatible_node(NULL, NULL, "mediatek,strobe_main");
	if (!uzel) {
		dev_err(dev, "нет узла mediatek,strobe_main\n");
		return -ENODEV;
	}
	t->chip = of_find_i2c_device_by_node(uzel);
	of_node_put(uzel);
	if (!t->chip)
		return -EPROBE_DEFER;	/* шина i2c-5 ещё не поднята */

	/* Убедиться, что на шине тот самый чип, и оставить его выключенным. */
	pinctrl_select_state(t->pins, t->hwen_high);
	usleep_range(1000, 2000);
	id = i2c_smbus_read_byte_data(t->chip, BF7_TORCH_REG_ID);
	i2c_smbus_write_byte_data(t->chip, BF7_TORCH_REG_ENABLE,
				  BF7_TORCH_ENABLE_OFF);
	pinctrl_select_state(t->pins, t->hwen_low);
	if (id < 0) {
		dev_err(dev, "чип вспышки на %s не отвечает: %d\n",
			dev_name(&t->chip->dev), id);
		ret = id;
		goto err_put;
	}

	t->led.name = "white:torch";
	t->led.max_brightness = ARRAY_SIZE(bf7_torch_kody);
	t->led.brightness_set_blocking = bf7_torch_set;
	t->led.flags = LED_CORE_SUSPENDRESUME;

	ret = devm_led_classdev_register(dev, &t->led);
	if (ret)
		goto err_put;

	platform_set_drvdata(pdev, t);
	dev_info(dev, "фонарик на %s (номер устройства 0x%02x), ступеней %u\n",
		 dev_name(&t->chip->dev), id, t->led.max_brightness);
	return 0;

err_put:
	put_device(&t->chip->dev);
	return ret;
}

static int bf7_torch_remove(struct platform_device *pdev)
{
	struct bf7_torch *t = platform_get_drvdata(pdev);

	bf7_torch_set(&t->led, LED_OFF);
	put_device(&t->chip->dev);
	return 0;
}

/* При выключении телефона фонарь не должен остаться гореть. */
static void bf7_torch_shutdown(struct platform_device *pdev)
{
	struct bf7_torch *t = platform_get_drvdata(pdev);

	if (t)
		bf7_torch_set(&t->led, LED_OFF);
}

static const struct of_device_id bf7_torch_of_match[] = {
	{ .compatible = "mediatek,flashlights_aw36515" },
	{ }
};
MODULE_DEVICE_TABLE(of, bf7_torch_of_match);

static struct platform_driver bf7_torch_driver = {
	.probe = bf7_torch_probe,
	.remove = bf7_torch_remove,
	.shutdown = bf7_torch_shutdown,
	.driver = {
		.name = "bf7-torch",
		.of_match_table = bf7_torch_of_match,
	},
};
module_platform_driver(bf7_torch_driver);

MODULE_DESCRIPTION("BF7: фонарик на светодиоде вспышки");
MODULE_LICENSE("GPL");
