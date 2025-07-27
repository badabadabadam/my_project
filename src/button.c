#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(button, LOG_LEVEL_ERR);

#define STACKSIZE 1024
#define PRIORITY 7

#define BUTTON_SHORT_PRESS_TIME 800 // ms

typedef enum {
    STATE_NONE = 0,
	STATE_A,
	STATE_B,
	STATE_BOTH,
	STATE_A_LONG,
	STATE_B_LONG,
    STATE_NUM,
} button_state_t;

static struct {
    struct k_mutex lock;
    button_state_t state;
    struct k_work_delayable work;
} input_data;
static struct gpio_callback button_cb_data;

#define SW0_NODE		DT_ALIAS(sw0)
#define SW1_NODE		DT_ALIAS(sw1)

static const struct gpio_dt_spec button_0 = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, {0});
static const struct gpio_dt_spec button_1 = GPIO_DT_SPEC_GET_OR(SW1_NODE, gpios, {0});

K_SEM_DEFINE(button_sem, 0, 1);

static void long_work_handler(struct k_work *work)
{
    k_mutex_lock(&input_data.lock, K_FOREVER);
    if (input_data.state == STATE_A) {
        input_data.state = STATE_NONE;
        settings_runtime_set("event/button_a_long", NULL, 0);
    } else if (input_data.state == STATE_B) {
        input_data.state = STATE_NONE;
        settings_runtime_set("event/button_b_long", NULL, 0);
    }
    k_mutex_unlock(&input_data.lock);
}

void button_event(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	if (pins & BIT(button_0.pin)) {
		bool pressed = gpio_pin_get_dt(&button_0);
		LOG_INF("Button 0 %s at %" PRIu32 "", pressed? "pressed":"released", k_cycle_get_32());
        if (pressed) {
            k_mutex_lock(&input_data.lock, K_FOREVER);
            if (input_data.state == STATE_NONE) {
                input_data.state = STATE_A;
                k_work_schedule(&input_data.work, K_MSEC(BUTTON_SHORT_PRESS_TIME));
            } else if (input_data.state == STATE_B) {
                input_data.state = STATE_NONE;
                k_work_cancel_delayable(&input_data.work);
                settings_runtime_set("event/button_ab", NULL, 0);
            }
            k_mutex_unlock(&input_data.lock);
        } else {
            k_mutex_lock(&input_data.lock, K_FOREVER);
            if (input_data.state == STATE_A) {
                input_data.state = STATE_NONE;
                k_work_cancel_delayable(&input_data.work);
                settings_runtime_set("event/button_a", NULL, 0);
            }
            k_mutex_unlock(&input_data.lock);
        }

	} else if (pins & BIT(button_1.pin)) {
        bool pressed = gpio_pin_get_dt(&button_1);
        LOG_INF("Button 1 %s at %" PRIu32 "", pressed? "pressed":"released", k_cycle_get_32());
        if (pressed) {
            k_mutex_lock(&input_data.lock, K_FOREVER);
            if (input_data.state == STATE_NONE) {
                input_data.state = STATE_B;
                k_work_schedule(&input_data.work, K_MSEC(BUTTON_SHORT_PRESS_TIME));
            } else if (input_data.state == STATE_A) {
                input_data.state = STATE_NONE;
                k_work_cancel_delayable(&input_data.work);
                settings_runtime_set("event/button_ab", NULL, 0);
            }
            k_mutex_unlock(&input_data.lock);
        } else {
            k_mutex_lock(&input_data.lock, K_FOREVER);
            if (input_data.state == STATE_B) {
                input_data.state = STATE_NONE;
                k_work_cancel_delayable(&input_data.work);
                settings_runtime_set("event/button_b", NULL, 0);
            }
            k_mutex_unlock(&input_data.lock);
        }
    } else {
        LOG_ERR("Unknown button event on pins: %08x", pins);
        return;
    }
}

static int button_init(void)
{
	int err;

    input_data.state = STATE_NONE;
    k_mutex_init(&input_data.lock); 
    k_work_init_delayable(&input_data.work, long_work_handler);

	if (!gpio_is_ready_dt(&button_0)) {
		LOG_ERR("Error: button device %s is not ready\n", button_0.port->name);
		return -1;
	}
	if (!gpio_is_ready_dt(&button_1)) {
		LOG_ERR("Error: button device %s is not ready\n", button_1.port->name);
		return -1;
	}
	err = gpio_pin_configure_dt(&button_0, GPIO_INPUT);
	if (err != 0) {
		LOG_ERR("Error %d: failed to configure %s pin %d", err, button_0.port->name, button_0.pin);
		return -1;
	}
	err = gpio_pin_configure_dt(&button_1, GPIO_INPUT);
	if (err != 0) {
		LOG_ERR("Error %d: failed to configure %s pin %d", err, button_1.port->name, button_1.pin);
		return -1;
	}

	err = gpio_pin_interrupt_configure_dt(&button_0, GPIO_INT_EDGE_BOTH);
	if (err != 0) {
		LOG_ERR("Error %d: failed to configure interrupt on %s pin %d", err, 
			button_0.port->name, button_0.pin);
		return -1;
	}

	err = gpio_pin_interrupt_configure_dt(&button_1, GPIO_INT_EDGE_BOTH);
	if (err != 0) {
		LOG_ERR("Error %d: failed to configure interrupt on %s pin %d", err, 
			button_1.port->name, button_1.pin);
		return -1;
	}

	gpio_init_callback(&button_cb_data, button_event, BIT(button_0.pin) | BIT(button_1.pin));
	gpio_add_callback(button_0.port, &button_cb_data);
	gpio_add_callback(button_1.port, &button_cb_data);

	return 0;
}

SYS_INIT(button_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

