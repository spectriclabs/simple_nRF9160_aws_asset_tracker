#ifndef LOCATION_MODULE_H__
#define LOCATION_MODULE_H__

#include <modem/location.h>


int location_mod_init(void);

void location_event_handler(const struct location_event_data *event_data);

int location_with_fallback_get(void);

void location_gnss_periodic_get(void);

#endif
