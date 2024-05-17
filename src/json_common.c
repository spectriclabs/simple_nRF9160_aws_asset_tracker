#include <zephyr/logging/log.h>

#include "json_common.h"

LOG_MODULE_REGISTER(json_common);

int json_shadow_construct(char *message, size_t size, struct shadow *payload)
{
	int err;
	const struct json_obj_descr parameters[] = {
		JSON_OBJ_DESCR_PRIM_NAMED(struct shadow, "uptime",
					  state.reported.uptime, JSON_TOK_NUMBER),
		JSON_OBJ_DESCR_PRIM_NAMED(struct shadow, "mcc",
					  state.reported.mcc, JSON_TOK_NUMBER),
		JSON_OBJ_DESCR_PRIM_NAMED(struct shadow, "mnc",
					  state.reported.mnc, JSON_TOK_NUMBER),
		JSON_OBJ_DESCR_PRIM_NAMED(struct shadow, "tac",
					  state.reported.tac, JSON_TOK_NUMBER),					  
		JSON_OBJ_DESCR_PRIM_NAMED(struct shadow, "eci",
					  state.reported.eci, JSON_TOK_NUMBER),	
	};
	const struct json_obj_descr reported[] = {
		JSON_OBJ_DESCR_OBJECT_NAMED(struct shadow, "reported", state.reported,
					    parameters),
	};
	const struct json_obj_descr root[] = {
		JSON_OBJ_DESCR_OBJECT(struct shadow, state, reported),
	};

	err = json_obj_encode_buf(root, ARRAY_SIZE(root), payload, message, size);
	if (err) {
		LOG_ERR("json_obj_encode_buf, error: %d", err);
		return err;
	}

	return 0;
}

int json_agnss_req_construct(char *message, size_t size, struct agnss_request *payload)
{
	int err;
	const struct json_obj_descr root[] = {
		JSON_OBJ_DESCR_PRIM_NAMED(struct agnss_request, "mcc",
					  mcc, JSON_TOK_NUMBER),
		JSON_OBJ_DESCR_PRIM_NAMED(struct agnss_request, "mnc",
					  mnc, JSON_TOK_NUMBER),
		JSON_OBJ_DESCR_PRIM_NAMED(struct agnss_request, "tac",
					  tac, JSON_TOK_NUMBER),					  
		JSON_OBJ_DESCR_PRIM_NAMED(struct agnss_request, "eci",
					  eci, JSON_TOK_NUMBER),
		JSON_OBJ_DESCR_PRIM_NAMED(struct agnss_request, "rsrp",
					  rsrp, JSON_TOK_NUMBER),
		JSON_OBJ_DESCR_PRIM_NAMED(struct agnss_request, "filtered",
					  rsrp, JSON_TOK_TRUE),
	};

	err = json_obj_encode_buf(root, ARRAY_SIZE(root), payload, message, size);
	if (err) {
		LOG_ERR("json_obj_encode_buf, error: %d", err);
		return err;
	}

	return 0;
}
