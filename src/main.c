/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <net/aws_iot.h>
#include <stdio.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/reboot.h>

#include <net/nrf_cloud_agnss.h>
#include <modem/modem_info.h>
#include <zephyr/drivers/gpio.h>

#include "json_common.h"
#include "location_module.h"
#include "modem_module.h"

LOG_MODULE_REGISTER(main);

/* Macro called upon a fatal error, reboots the device. */
#define FATAL_ERROR()					\
	LOG_ERR("Fatal error! Rebooting the device.");	\
	LOG_PANIC();					\
	IF_ENABLED(CONFIG_REBOOT, (sys_reboot(0)))

//////////////////////////////////////////////////////////////////////////////
// LEDs and Buttons
#define GPIO_NODE        DT_NODELABEL(gpio0)
#define RED_LED_NODE		DT_ALIAS(led0)
#define GREEN_LED_NODE		DT_ALIAS(led1)
#define BLUE_LED_NODE		DT_ALIAS(led2)

#define LED_OFF 0
#define LED_ON !LED_OFF

typedef enum {
	RED,
	GREEN,
	BLUE,
	MAGENTA,
	CYAN,
	YELLOW
} led_color_t;

static struct gpio_dt_spec red_led = GPIO_DT_SPEC_GET(RED_LED_NODE, gpios);
static struct gpio_dt_spec green_led = GPIO_DT_SPEC_GET(GREEN_LED_NODE, gpios);
static struct gpio_dt_spec blue_led = GPIO_DT_SPEC_GET(BLUE_LED_NODE, gpios);

static const struct device *gpio_dev;
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
// AWS
#define AWS_CLOUD_CLIENT_ID_LEN (sizeof(CONFIG_AWS_IOT_CLIENT_ID_STATIC) - 1)

#define AGNSS_REQUEST_TOPIC "nrfcloud/agps/get"
#define AGNSS_REQUEST_TOPIC_IDX 0

#define AGNSS_RESPONSE_TOPIC "nrfcloud/agps"
#define AGNSS_RESPONSE_TOPIC_LEN 1

static struct aws_iot_config config;
static char client_id_buf[AWS_CLOUD_CLIENT_ID_LEN + 1];

static struct aws_iot_topic_data pub_topics[1] = {
        [AGNSS_REQUEST_TOPIC_IDX].str = AGNSS_REQUEST_TOPIC,
        [AGNSS_REQUEST_TOPIC_IDX].len = strlen(AGNSS_REQUEST_TOPIC),
};

const struct aws_iot_topic_data sub_topics[CONFIG_AWS_IOT_APP_SUBSCRIPTION_LIST_COUNT] = {
        [0].str = AGNSS_RESPONSE_TOPIC,
        [0].len = strlen(AGNSS_RESPONSE_TOPIC),
};

K_SEM_DEFINE(aws_connected, 0, 1);
//////////////////////////////////////////////////////////////////////////////

static void print_hex(const char* buf, const size_t len) {
	for(size_t i=0; i<len; ++i) {
    	printf("%02X", (unsigned char)buf[i]);
	}
	printf("\n");
}

static void aws_iot_event_handler(const struct aws_iot_evt *const evt)
{
    int err;

	switch (evt->type) {
		case AWS_IOT_EVT_CONNECTING:
			LOG_INF("Connecting to AWS");
			break;
		case AWS_IOT_EVT_CONNECTED:
			LOG_INF("Connecting to AWS");
			if (evt->data.persistent_session) {
				LOG_WRN("Persistent session is enabled, using subscriptions "
						"from the previous session");
			}
			break;
		case AWS_IOT_EVT_READY:
			LOG_INF("AWS Ready");
			k_sem_give(&aws_connected);
			break;
		case AWS_IOT_EVT_DATA_RECEIVED:
			LOG_INF("AWS_IOT_EVT_DATA_RECEIVED");

			LOG_INF("Received message %d bytes on topic: \"%.*s\"",
									evt->data.msg.len,
									evt->data.msg.topic.len,
									evt->data.msg.topic.str);
			//print_hex(evt->data.msg.ptr, evt->data.msg.len);
			if (strncmp(evt->data.msg.topic.str, AGNSS_RESPONSE_TOPIC, evt->data.msg.topic.len) == 0) {
				err = nrf_cloud_agnss_process(evt->data.msg.ptr, evt->data.msg.len);
				if (err) {
					LOG_ERR("Unable to process A-GNSS data, error: %d", err);
				}
			}
			break;
		case AWS_IOT_EVT_DISCONNECTED:
			LOG_INF("AWS Disconnected");
			break;
		case AWS_IOT_EVT_ERROR:
			LOG_ERR("AWS Err");
			break;
		default:
            LOG_DBG("AWS event %d", evt->type);
	}
}

static int update_aws_shadow() {
	// Get modem info
	int err;
	static struct modem_param_info modem_info = {0};

	err = modem_info_params_init(&modem_info);
	if (err) {
		return err;
	}
	
	err = modem_info_params_get(&modem_info);
	if (err) {
		return err;
	}

	char buf[4096];
	bool ack = false;

	struct shadow payload = {
		.state.reported.uptime = k_uptime_get(),
		.state.reported.mcc = modem_info.network.mcc.value,
		.state.reported.mnc = modem_info.network.mnc.value,
		.state.reported.tac = modem_info.network.area_code.value,
		.state.reported.eci = modem_info.network.cellid_dec,
	};

	err = json_shadow_construct(buf, sizeof(buf), &payload);
	if (err) {
		LOG_ERR("json_shadow_construct, error: %d", err);
		FATAL_ERROR();
		return err;
	}

	struct aws_iot_data msg = {
		.ptr = buf,
		.len = strlen(buf),
		.message_id = 1,
		.qos = ack ? MQTT_QOS_1_AT_LEAST_ONCE : MQTT_QOS_0_AT_MOST_ONCE,
		.topic.type = AWS_IOT_SHADOW_TOPIC_UPDATE,
	};

	LOG_INF("Publishing message: %s to AWS IoT shadow", buf);

	err = aws_iot_send(&msg);
	if (err) {
		printf("aws_iot_send, error: %d\n", err);
		return err;
	}

	return 0;
}

static int aws_agnss_req() {
	// Get modem info
	int err;
	static struct modem_param_info modem_info = {0};

	err = modem_info_params_init(&modem_info);
	if (err) {
		return err;
	}
	
	err = modem_info_params_get(&modem_info);
	if (err) {
		return err;
	}

	char buf[4096];
	bool ack = false;

	struct agnss_request payload = {
		.mcc = modem_info.network.mcc.value,
		.mnc = modem_info.network.mnc.value,
		.tac = modem_info.network.area_code.value,
		.eci = modem_info.network.cellid_dec,
		.rsrp = modem_info.network.rsrp.value,
		.filtered = true,
		.mask = 5,
	};

	err = json_agnss_req_construct(buf, sizeof(buf), &payload);
	if (err) {
		LOG_ERR("json_payload_construct, error: %d", err);
		FATAL_ERROR();
		return err;
	}

	struct aws_iot_data msg = {
		.ptr = buf,
		.len = strlen(buf),
		.message_id = 1,
		.qos = ack ? MQTT_QOS_1_AT_LEAST_ONCE : MQTT_QOS_0_AT_MOST_ONCE,
		.topic = pub_topics[AGNSS_REQUEST_TOPIC_IDX],
	};

	LOG_INF("Publishing message: %s to AWS IoT shadow", buf);

	err = aws_iot_send(&msg);
	if (err) {
		printf("aws_iot_send, error: %d\n", err);
		return err;
	}

	return 0;
}

int gpio_init(void)
{
	int err;

	gpio_dev = DEVICE_DT_GET(GPIO_NODE);
	if (!gpio_dev) {
		LOG_ERR("LED initialization failed, err %d", err);
		return err;
	}

	gpio_pin_configure_dt(&red_led, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&green_led, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&blue_led, GPIO_OUTPUT_INACTIVE);

	return 0;
}

void set_led(int red, int green, int blue)
{
	gpio_pin_set_dt(&red_led, red);
	gpio_pin_set_dt(&green_led, green);
	gpio_pin_set_dt(&blue_led, blue);
}

int main(void)
{
	int err;

	printf("Simple AWS Asset Tracker %s\n", CONFIG_AWS_IOT_CLIENT_ID_STATIC);
	err = gpio_init();
	if (err) {
		LOG_ERR("GPIO initialization failed, err %d", err);
		FATAL_ERROR();
		return err;
	}
	
	//////////////////////////////////////////////////////////////////////////
	// Setup Modem and connect to LTE network

	set_led(LED_ON /* red */, LED_OFF /* green */, LED_OFF /* blue */);
	
	err = modem_mod_init();
	if (err) {
		LOG_ERR("Modem initialization failed, err %d", err);
		FATAL_ERROR();
		return err;
	}

	err = modem_mod_connect();
	if (err) {
		LOG_ERR("Modem connection failed, err %d", err);
		FATAL_ERROR();
		return err;
	}

	set_led(LED_ON /* red */, LED_OFF /* green */, LED_ON /* blue */);

	//////////////////////////////////////////////////////////////////////////
	// Try to get initial location and then setup periodic location

	err = location_mod_init();
	if (err) {
		LOG_ERR("location module init error: %d", err);
		FATAL_ERROR();
		return err;
	}
	
	LOG_INF("Requesting location...");
	err = location_with_fallback_get();
	if (err) {
		LOG_ERR("Requesting location failed, error: %d", err);
	}

	LOG_INF("Periodic location...");
	location_gnss_periodic_get();

	set_led(LED_OFF /* red */, LED_ON /* green */, LED_OFF /* blue */);

	//////////////////////////////////////////////////////////////////////////
	// Connect to AWS IoT
	
	// AWS
	LOG_INF("Connecting to AWS...");
	// AWS Hello World
	snprintf(client_id_buf, sizeof(client_id_buf), "%s", CONFIG_AWS_IOT_CLIENT_ID_STATIC);

	config.client_id = client_id_buf;
	config.client_id_len = strlen(client_id_buf);

	err = aws_iot_init(&config, aws_iot_event_handler);
	if (err) {
		LOG_ERR("aws_iot_init, error: %d", err);
		FATAL_ERROR();
		return err;
	}

	LOG_INF("Subscribing to topics...");
	err = aws_iot_subscription_topics_add(sub_topics, ARRAY_SIZE(sub_topics));
	if (err) {
        LOG_ERR("aws_iot_subscription_topics_add, error: %d", err);
		FATAL_ERROR();
        return err;
	}

	err = aws_iot_connect(&config);
	if (err) {
		LOG_ERR("aws_iot_init, error: %d", err);
		FATAL_ERROR();
		return err;
	}

	LOG_INF("Waiting for AWS...");
	k_sem_take(&aws_connected, K_FOREVER);
	LOG_INF("Connected to AWS network!");

	set_led(LED_OFF /* red */, LED_OFF /* green */, LED_ON /* blue */);

	update_aws_shadow();

	aws_agnss_req();

	while (1 == 1) {
		err = aws_iot_ping();
		if (err) {
			printf("aws_iot_ping, error: %d\n", err);
		}
		printf("waiting...\n");
		
		k_sleep(K_MSEC(10000));
	}
	
	return 0;
}
