#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(led, LOG_LEVEL_INF);

#define STACKSIZE 1024
#define PRIORITY 7

static const struct gpio_dt_spec red_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec green_led = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec blue_led = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

K_SEM_DEFINE(led_sem, 0, 1);

typedef struct {
    bool red;
    bool green;
    bool blue;
    k_timeout_t duration;
} led_pattern_t;

static const led_pattern_t led_off[] __maybe_unused = {
    { .red = false, .green = false, .blue = false, .duration = K_NO_WAIT },
};
static const led_pattern_t power_on[] __maybe_unused = {
    { .red = false, .green = false, .blue = true, .duration = K_MSEC(3000) },
    { .red = false, .green = false, .blue = false, .duration = K_NO_WAIT },
};
static const led_pattern_t power_off[] __maybe_unused = {
    { .red = true, .green = false, .blue = false, .duration = K_MSEC(3000) },
    { .red = false, .green = false, .blue = false, .duration = K_NO_WAIT },
};

static const led_pattern_t red_on[] __maybe_unused = {
    { .red = true, .green = false, .blue = false, .duration = K_MSEC(1000) },
    { .red = false, .green = false, .blue = false, .duration = K_NO_WAIT },
};
static const led_pattern_t green_on[] __maybe_unused = {
    { .red = false, .green = true, .blue = false, .duration = K_MSEC(1000) },
    { .red = false, .green = false, .blue = false, .duration = K_NO_WAIT },
};
static const led_pattern_t blue_on[] __maybe_unused = {
    { .red = false, .green = false, .blue = true, .duration = K_MSEC(1000) },
    { .red = false, .green = false, .blue = false, .duration = K_NO_WAIT },
};
static const led_pattern_t blue_blinking[] __maybe_unused = {
    { .red = false, .green = false, .blue = true, .duration = K_MSEC(500) },
    { .red = false, .green = false, .blue = false, .duration = K_MSEC(500) },
    { .red = false, .green = false, .blue = false, .duration = K_NO_WAIT },
};
static const led_pattern_t red_blinking[] __maybe_unused = {
    { .red = true, .green = false, .blue = false, .duration = K_MSEC(500) },
    { .red = false, .green = false, .blue = false, .duration = K_MSEC(500) },
    { .red = false, .green = false, .blue = false, .duration = K_NO_WAIT },
};
static const led_pattern_t green_blinking[] __maybe_unused = {
    { .red = false, .green = true, .blue = false, .duration = K_MSEC(500) },
    { .red = false, .green = false, .blue = false, .duration = K_MSEC(500) },
    { .red = false, .green = false, .blue = false, .duration = K_NO_WAIT },
};
static const led_pattern_t yellow_blinking[] __maybe_unused = {
    { .red = true, .green = true, .blue = false, .duration = K_MSEC(500) },
    { .red = false, .green = false, .blue = false, .duration = K_MSEC(500) },
    { .red = false, .green = false, .blue = false, .duration = K_NO_WAIT },
};
static const led_pattern_t red_blue_alternating[] __maybe_unused = {
    { .red = true, .green = false, .blue = false, .duration = K_MSEC(100) },
    { .red = false, .green = false, .blue = true, .duration = K_MSEC(100) },
    { .red = false, .green = false, .blue = false, .duration = K_NO_WAIT },
};

struct {
    const led_pattern_t *pattern;
    const led_pattern_t *event_pattern;
    size_t index;   
} led_state = {
    .pattern = NULL,
    .event_pattern = NULL,
    .index = 0,
};

static k_timeout_t set_leds(const led_pattern_t *pattern)
{
    gpio_pin_set_dt(&red_led, pattern->red);
    gpio_pin_set_dt(&green_led, pattern->green);
    gpio_pin_set_dt(&blue_led, pattern->blue);
    return pattern->duration;
}

static void set_led_pattern(const led_pattern_t *pattern)
{
    led_state.pattern = pattern;
    if (!led_state.event_pattern) {
        led_state.index = 0;
        k_sem_give(&led_sem);
    }
}

static void set_led_event_pattern(const led_pattern_t *pattern)
{
    led_state.event_pattern = pattern;
    led_state.index = 0;
    k_sem_give(&led_sem);
}

static void led_thread(void)
{
	if (!gpio_is_ready_dt(&red_led)) {
		LOG_ERR("Error: device %s not ready", red_led.port->name);
		return;
	}
	if (!gpio_is_ready_dt(&green_led)) {
		LOG_ERR("Error: device %s not ready", green_led.port->name);
		return;
	}
	if (!gpio_is_ready_dt(&blue_led)) {
		LOG_ERR("Error: device %s not ready", blue_led.port->name);
		return;
	}

    gpio_pin_configure_dt(&red_led, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure_dt(&green_led, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure_dt(&blue_led, GPIO_OUTPUT_ACTIVE);

    set_led_event_pattern(led_off);
    k_timeout_t duration = K_FOREVER;
	while (1) {
        k_sem_take(&led_sem, duration);
        if (led_state.event_pattern) {
            duration = set_leds(&led_state.event_pattern[led_state.index]);
            led_state.index++;
            if (K_TIMEOUT_EQ(led_state.event_pattern[led_state.index].duration, K_NO_WAIT)) {
                led_state.index = 0;
                led_state.event_pattern = NULL; // Clear event pattern after completion
            }
        } else if (led_state.pattern) {
            duration = set_leds(&led_state.pattern[led_state.index]);
            led_state.index++;
            if (K_TIMEOUT_EQ(led_state.pattern[led_state.index].duration, K_NO_WAIT)) {
                led_state.index = 0;
            }
        } else {
            duration = K_FOREVER;
        }

	}
}

K_THREAD_DEFINE(led_id, STACKSIZE, led_thread, NULL, NULL, NULL, PRIORITY, 0, 0);




int led_handle_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;
	size_t name_len;
	// int err;

	name_len = settings_name_next(name, &next);
	if (!next) {
		if (!strncmp(name, "poweron", name_len)) {
			LOG_INF("<led/poweron>");
            set_led_event_pattern(power_on);
            set_led_pattern(blue_blinking); // Set a default pattern after power on
			return 0;
		}
		if (!strncmp(name, "poweroff", name_len)) {
			LOG_INF("<led/poweroff>");
            set_led_event_pattern(power_off);
            set_led_pattern(led_off); // Set a default pattern after power off
			return 0;
		}
		if (!strncmp(name, "standby", name_len)) {
			LOG_INF("<led/standby>");
            set_led_pattern(blue_blinking); 
			return 0;
		}
		if (!strncmp(name, "ready", name_len)) {
			LOG_INF("<led/ready>");
            set_led_pattern(green_blinking); 
			return 0;
		}
		if (!strncmp(name, "pairing", name_len)) {
			LOG_INF("<led/pairing>");
            set_led_pattern(red_blue_alternating); 
			return 0;
		}
	}
	return -ENOENT;
}
/* static subtree handler */
SETTINGS_STATIC_HANDLER_DEFINE(led, "led", NULL, led_handle_set, NULL, NULL);


