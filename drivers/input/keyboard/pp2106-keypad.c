/*
 *  drivers/input/keyboard/pp2016-keypad.c
 *
 *  Copyright (c) 2012 LGE.
 * 
 *  All source code in this file is licensed under the following license
 *  except where indicated.
 * 
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 * 
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *  See the GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, you can find it at http://www.fsf.org
 */

#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/input/pp2106-keypad.h>

#define KEY_DRIVER_NAME "pp2106-keypad"

static const char *kbd_name = "pp2106";

enum kbd_inevents {
	PP2106_IN_KEYPRESS        = 1,
	PP2106_IN_KEYRELEASE      = 0,
};

enum {
	GPIO_LOW_VALUE  = 0,
	GPIO_HIGH_VALUE = 1
};

enum {
	QWERTY_START_BIT = 0,
	QWERTY_1ST_BIT7,
	QWERTY_1ST_BIT6,
	QWERTY_1ST_BIT5,
	QWERTY_1ST_BIT4,
	QWERTY_1ST_BIT3,
	QWERTY_1ST_BIT2,
	QWERTY_1ST_BIT1,
	QWERTY_1ST_BIT0,
	QWERTY_ACK_BIT,
	QWERTY_MAX_BIT
} ;

#define KEY_SCL_PIN (pp2106_pdata->scl_pin)
#define KEY_SDA_PIN (pp2106_pdata->sda_pin)
#define KEY_IRQ_PIN (pp2106_pdata->irq_pin)

#define QWERTY_SDA_OUTPUT() { gpio_direction_output(KEY_SDA_PIN, 0); udelay(25);}
#define QWERTY_SDA_HIGH()   { gpio_set_value(KEY_SDA_PIN,GPIO_HIGH_VALUE); udelay(25); }
#define QWERTY_SDA_LOW()    { gpio_set_value(KEY_SDA_PIN,GPIO_LOW_VALUE); udelay(25); }
#define QWERTY_SDA_INPUT()  { gpio_direction_input(KEY_SDA_PIN); udelay(25);}
#define QWERTY_SDA_READ()   gpio_get_value(KEY_SDA_PIN)
#define QWERTY_SCL_OUTPUT() { gpio_direction_output(KEY_SCL_PIN, 0); udelay(25); }
#define QWERTY_SCL_HIGH()   { gpio_set_value(KEY_SCL_PIN,GPIO_HIGH_VALUE); udelay(25); }
#define QWERTY_SCL_LOW()    { gpio_set_value(KEY_SCL_PIN,GPIO_LOW_VALUE); udelay(25); }
#define QWERTY_IRQ_HIGH()   { gpio_set_value(KEY_IRQ_PIN,GPIO_HIGH_VALUE); udelay(25); }

/*
 * The qwerty_kbd_record structure consolates all the data/variables
 * specific to managing the single instance of the keyboard.
 */
static struct input_dev *pp2106_kbd_dev;
static struct pp2106_keypad_platform_data *pp2106_pdata;
static struct work_struct pp2106_irqwork;

static __inline void pp2106_send_ack(void)
{
	QWERTY_SDA_OUTPUT();
	QWERTY_SDA_LOW();

	QWERTY_SCL_LOW();
	gpio_direction_input(KEY_SDA_PIN);
	QWERTY_SCL_HIGH();
}

static __inline int pp2106_get_data(uint32_t *p_data)
{
	int trigger_count;
	int first_bit_count = 0;

	QWERTY_SDA_OUTPUT();
	QWERTY_SCL_HIGH();
	QWERTY_SDA_LOW();

	for (trigger_count=QWERTY_START_BIT;
			trigger_count < QWERTY_MAX_BIT; trigger_count++) {
		if (trigger_count == QWERTY_START_BIT) {
			QWERTY_SCL_LOW();
			QWERTY_SDA_INPUT();
			QWERTY_SCL_HIGH();
		} else if (trigger_count >= QWERTY_1ST_BIT7 &&
				trigger_count <= QWERTY_1ST_BIT0) {
			QWERTY_SCL_LOW();
			if (QWERTY_SDA_READ()) {
				*p_data |= 0x80 >> (first_bit_count);
			}

			first_bit_count++;
			QWERTY_SCL_HIGH();
		} else if (trigger_count == QWERTY_ACK_BIT) {
			pp2106_send_ack();
		}
	}

	return 0;
}

static irqreturn_t pp2106_irqhandler(int irq, void *dev_id)
{
	schedule_work(&pp2106_irqwork);

	return IRQ_HANDLED;
}

static int pp2106_config_gpio(void)
{
	int rc = 0;

	rc = gpio_request(pp2106_pdata->irq_pin, "pp2106_irq");
	if (rc)
		pr_err("gpio_request failed on pin %d (rc=%d)\n",
				pp2106_pdata->irq_pin, rc);

	rc = gpio_direction_input(pp2106_pdata->irq_pin);
	if (rc)
		pr_err("gpio_direction_input failed on "
				"pin %d (rc=%d)\n", pp2106_pdata->irq_pin, rc);

	rc = gpio_request(pp2106_pdata->reset_pin, "pp2106_reset");
	if (rc)
		pr_err("gpio_request failed on pin %d (rc=%d)\n",
				pp2106_pdata->reset_pin, rc);

	rc = gpio_direction_output(pp2106_pdata->reset_pin, 1);
	if (rc)
		pr_err("gpio_direction_output failed on "
				"pin %d (rc=%d)\n", pp2106_pdata->reset_pin, rc);

	rc = gpio_request(pp2106_pdata->sda_pin, "pp2106_sda");
	if (rc)
		pr_err("gpio_request failed on pin %d (rc=%d)\n",
				pp2106_pdata->sda_pin, rc);

	rc = gpio_direction_input(pp2106_pdata->sda_pin);
	if (rc)
		pr_err("gpio_direction_input failed on "
				"pin %d (rc=%d)\n", pp2106_pdata->sda_pin, rc);

	rc = gpio_request(pp2106_pdata->scl_pin, "pp2106_scl");
	if (rc)
		pr_err("gpio_request failed on pin %d (rc=%d)\n",
				pp2106_pdata->scl_pin, rc);

	rc = gpio_direction_output(pp2106_pdata->scl_pin, 1);
	if (rc)
		pr_err("gpio_direction_output failed on "
				"pin %d (rc=%d)\n", pp2106_pdata->scl_pin, rc);

	return rc;
}

static void pp2106_hwreset(void)
{
	gpio_set_value(pp2106_pdata->reset_pin, 1);
	msleep(25);
	gpio_set_value(pp2106_pdata->reset_pin, 0);
	msleep(25);
	gpio_set_value(pp2106_pdata->reset_pin, 1);
	msleep(25);
}

static void pp2106_fetchkeys(struct work_struct *work)
{
	u32 scancode = 0;
	u32 buf = 0;
	u8 keystate = 0;  /* press = 1 , release = 0 */
	u8 key_col, key_row;

	pp2106_get_data(&buf);
	keystate = (buf & 0x80) ? PP2106_IN_KEYRELEASE : PP2106_IN_KEYPRESS;

	key_col = key_row = buf;

	key_col &= 0x0f;
	if (key_col)
		key_col -= 1;

	key_row >>= 4;
	key_row &= 0x07;

	scancode = (unsigned int)pp2106_pdata->keycode[2 * (key_row * pp2106_pdata->keypad_col + key_col)];
	printk("%s:scancode = %d, keystate = %d\n", __func__,scancode,keystate);

	printk("%s:Keypad : row <0x%x>, column <0x%x>, keycode <<%d>>\n",
			__func__, key_row, key_col, scancode);

	if (scancode) {
		input_report_key(pp2106_kbd_dev, scancode, keystate);
		input_sync(pp2106_kbd_dev);
	}
}

struct input_dev *qwerty_get_input_dev(void)
{
	return pp2106_kbd_dev;
}
EXPORT_SYMBOL(qwerty_get_input_dev);

static int pp2106_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int pp2106_resume(struct platform_device *pdev)
{
	/* future capability */
	QWERTY_SDA_HIGH();
	QWERTY_SCL_HIGH();
	QWERTY_IRQ_HIGH();
	pp2106_hwreset();

	pr_err("%s: IRQ: %d, RESET: %d, SDA: %d, SCL: %d\n", __func__,
		gpio_get_value(pp2106_pdata->irq_pin),
		gpio_get_value(pp2106_pdata->reset_pin),
		gpio_get_value(pp2106_pdata->sda_pin),
		gpio_get_value(pp2106_pdata->scl_pin));

	return 0;
}

static int pp2106_probe(struct platform_device *pdev)
{
	int rc = 0;
	int key_idx;
	unsigned keycode = KEY_UNKNOWN;

	printk("%s :  probe start!!\n", __func__);

	if (!pdev || !pdev->dev.platform_data) {
		pr_err("%s : pdev or platform data is null\n", __func__);
		return -ENODEV;
	}

	pp2106_pdata = (struct pp2106_keypad_platform_data *)pdev->dev.platform_data;

	if (!pp2106_pdata->reset_pin 	|| !pp2106_pdata->irq_pin		||
		!pp2106_pdata->sda_pin 		|| !pp2106_pdata->scl_pin		||
		!pp2106_pdata->keypad_row	|| !pp2106_pdata->keypad_col	||
		!pp2106_pdata->keycode) {
		pr_err("%s : platform data is invalid\n", __func__);
		return -ENODEV;
	}

	pp2106_kbd_dev = input_allocate_device();
	if (!pp2106_kbd_dev) {
		pr_err("%s: not enough memory for input device\n", __func__);
		return -ENOMEM;
	}

	pp2106_kbd_dev->name = KEY_DRIVER_NAME;
	pp2106_kbd_dev->phys = "pp2106/input1";
	pp2106_kbd_dev->id.bustype = BUS_HOST;
	pp2106_kbd_dev->id.vendor = 0x0001;
	pp2106_kbd_dev->id.product = 0x0001;
	pp2106_kbd_dev->id.version = 0x0100;
	pp2106_kbd_dev->dev.parent = &pdev->dev;
	pp2106_kbd_dev->evbit[0] = BIT_MASK(EV_KEY);

	pp2106_kbd_dev->keycode = pp2106_pdata->keycode;
	pp2106_kbd_dev->keycodesize = sizeof(unsigned short);
	pp2106_kbd_dev->keycodemax = pp2106_pdata->keypad_row * pp2106_pdata->keypad_col;
	pp2106_kbd_dev->mscbit[0] = 0;

	for (key_idx = 0; key_idx <= pp2106_kbd_dev->keycodemax; key_idx++) {
		keycode = pp2106_pdata->keycode[2 * key_idx];
		if (keycode != KEY_UNKNOWN)
				set_bit(keycode, pp2106_kbd_dev->keybit);
	}
	rc = pp2106_config_gpio();
	if (rc) {
		pr_err("%s : gpio setting failed\n", __func__);
		return rc;
	}

	pp2106_hwreset();

	INIT_WORK(&pp2106_irqwork, pp2106_fetchkeys);

	rc = request_irq(gpio_to_irq(pp2106_pdata->irq_pin), &pp2106_irqhandler,
			IRQF_TRIGGER_FALLING, kbd_name, NULL);
	if (rc < 0) {
		pr_err("Could not register for  %s interrupt "
				"(rc = %d)\n", kbd_name, rc);
		rc = -EIO;
	}

	rc = input_register_device(pp2106_kbd_dev);
	if (rc)
		pr_err("%s : input_register_device failed\n", __func__);

	printk("%s :  probe End!!\n", __func__);

	return rc;
}

static struct platform_driver qwerty_kbd_driver = {
	.driver = {
		.name = KEY_DRIVER_NAME,
		.owner = THIS_MODULE,
	},
	.probe	 = pp2106_probe,
	.suspend = pp2106_suspend,
	.resume  = pp2106_resume,
};

static int __init pp2106_init(void)
{
	return platform_driver_register(&qwerty_kbd_driver);
}

static void __exit pp2106_exit(void)
{
	platform_driver_unregister(&qwerty_kbd_driver);
}

module_init(pp2106_init);
module_exit(pp2106_exit);

MODULE_DESCRIPTION("PP2106 QWERTY keyboard driver");
MODULE_LICENSE("GPL v2");
