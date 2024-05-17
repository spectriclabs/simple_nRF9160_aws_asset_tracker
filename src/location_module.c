#include <modem/location.h>
#include <zephyr/logging/log.h>

#include "location_module.h"

LOG_MODULE_REGISTER(location_module);

static K_SEM_DEFINE(location_event, 0, 1);

int location_mod_init(void)
{
    int err;
    
    LOG_INF("Setting up location...");
	err = location_init(location_event_handler);
	if (err) {
		LOG_ERR("Initializing the Location library failed, error: %d", err);
		return err;
	}

    return 0;
}

void location_event_handler(const struct location_event_data *event_data)
{
	switch (event_data->id) {
	case LOCATION_EVT_LOCATION:
		printk("Got location:\n");
		printk("  method: %s\n", location_method_str(event_data->method));
		printk("  latitude: %.06f\n", event_data->location.latitude);
		printk("  longitude: %.06f\n", event_data->location.longitude);
		printk("  accuracy: %.01f m\n", event_data->location.accuracy);
		if (event_data->location.datetime.valid) {
			printk("  date: %04d-%02d-%02d\n",
				event_data->location.datetime.year,
				event_data->location.datetime.month,
				event_data->location.datetime.day);
			printk("  time: %02d:%02d:%02d.%03d UTC\n",
				event_data->location.datetime.hour,
				event_data->location.datetime.minute,
				event_data->location.datetime.second,
				event_data->location.datetime.ms);
		}
		printk("  Google maps URL: https://maps.google.com/?q=%.06f,%.06f\n\n",
			event_data->location.latitude, event_data->location.longitude);
		break;

	case LOCATION_EVT_TIMEOUT:
		printk("Getting location timed out\n\n");
		break;

	case LOCATION_EVT_ERROR:
		printk("Getting location failed\n\n");
		break;

	case LOCATION_EVT_GNSS_ASSISTANCE_REQUEST:
		printk("Getting location assistance requested (A-GNSS). Not doing anything.\n\n");
		break;

	case LOCATION_EVT_GNSS_PREDICTION_REQUEST:
		printk("Getting location assistance requested (P-GPS). Not doing anything.\n\n");
		break;

	default:
		printk("Getting location: Unknown event\n\n");
		break;
	}

	k_sem_give(&location_event);
}

int location_with_fallback_get(void)
{
	int err;
	struct location_config config;
	enum location_method methods[] = {LOCATION_METHOD_GNSS, LOCATION_METHOD_CELLULAR};

	location_config_defaults_set(&config, ARRAY_SIZE(methods), methods);
	/* GNSS timeout is set to 1 second to force a failure. */
	config.methods[0].gnss.timeout = 100 * MSEC_PER_SEC;
	/* Default cellular configuration may be overridden here. */
	config.methods[1].cellular.timeout = 40 * MSEC_PER_SEC;

	LOG_INF("Requesting location with long GNSS timeout with fallback to cellular...\n");

	err = location_request(&config);
	if (err) {
		return err;
	}

	k_sem_take(&location_event, K_FOREVER);

	return 0;
}

void location_gnss_periodic_get(void)
{
	int err;
	struct location_config config;
	enum location_method methods[] = {LOCATION_METHOD_GNSS, LOCATION_METHOD_CELLULAR};

	location_config_defaults_set(&config, ARRAY_SIZE(methods), methods);
	config.interval = 30;

	printk("Requesting 30s periodic GNSS location with cellular fallback...\n");

	err = location_request(&config);
	if (err) {
		printk("Requesting location failed, error: %d\n", err);
		return;
	}
}
