#ifndef JSON_COMMON_H__
#define JSON_COMMON_H__

#include <zephyr/data/json.h>

struct shadow {
	struct {
		struct {
			uint32_t uptime;
			uint16_t mcc;
			uint16_t mnc;
			uint16_t tac;
			double eci;
		} reported;
	} state;
};

struct agnss_request {
	int mcc;
	int mnc;
	int tac;
	double eci;
	int rsrp;
	bool filtered;
	int mask;
};

int json_shadow_construct(char *message, size_t size, struct shadow *payload);

int json_agnss_req_construct(char *message, size_t size, struct agnss_request *payload);

#endif
