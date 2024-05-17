#include <zephyr/logging/log.h>
#include <modem/modem_jwt.h>
#include <modem/modem_key_mgmt.h>
#include <modem/modem_info.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>

#include "modem_module.h"
#include "certificates.h"

LOG_MODULE_REGISTER(modem_module);

static K_SEM_DEFINE(lte_connected, 0, 1);
static K_SEM_DEFINE(time_update_finished, 0, 1);


void lte_handler(const struct lte_lc_evt *const evt)
{
	switch (evt->type) {
        case LTE_LC_EVT_NW_REG_STATUS:
                if (evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME &&
                    evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING) {
                        break;
                }

                LOG_INF("Connected to: %s network\n",
                       evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ? "home" : "roaming");

                k_sem_give(&lte_connected);
                break;

        case LTE_LC_EVT_PSM_UPDATE:
			LOG_INF("PSM mode: %s", evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ? "Connected" : "Idle");
			break;
        case LTE_LC_EVT_EDRX_UPDATE:
		case LTE_LC_EVT_RRC_UPDATE:
			LOG_INF("RRC mode: %s", evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ? "Connected" : "Idle");
			break;
        case LTE_LC_EVT_CELL_UPDATE:
        case LTE_LC_EVT_LTE_MODE_UPDATE:
        case LTE_LC_EVT_TAU_PRE_WARNING:
        case LTE_LC_EVT_NEIGHBOR_CELL_MEAS:
        case LTE_LC_EVT_MODEM_SLEEP_EXIT_PRE_WARNING:
        case LTE_LC_EVT_MODEM_SLEEP_EXIT:
        case LTE_LC_EVT_MODEM_SLEEP_ENTER:
        case LTE_LC_EVT_MODEM_EVENT:
                /* Handle LTE events */
                break;

        default:
                break;
	}
}

void enable_xtal(void)
{
	struct onoff_manager *clk_mgr;
	static struct onoff_client cli = {};

	clk_mgr = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
	sys_notify_init_spinwait(&cli.notify);
	(void)onoff_request(clk_mgr, &cli);
}

void date_time_evt_handler(const struct date_time_evt *evt)
{
	k_sem_give(&time_update_finished);
}

int modem_mod_init(void) {
    int err;

    LOG_INF("Initializing modem library...");
	err = nrf_modem_lib_init();
	if (err) {
		LOG_ERR("Modem initialization failed, err %d", err);
		return err;
	}

    // in some examples, it was considered necessary to 
    // adjust the clock control to get the modem AT commands
    // to work
    enable_xtal(); 

	modem_info_init();

    // Get the modem UUID
    // TODO consider replacing with CONFIG_HW_ID_LIBRARY
    struct nrf_device_uuid dev = {0};
	err = modem_jwt_get_uuids(&dev, NULL);
	if (err) {
		LOG_ERR("Get device UUID error: %d", err);
	} else {
		printk("Modem attestation %s\n", dev.str);
	}

	// Get Modem IEMI/CCID
	LOG_INF("Getting modem info...");
	char imei[32] = {'\0',};
	char ccid[32] = {'\0',};

	//modem_info_params_get(&modem_param);
	modem_info_string_get(MODEM_INFO_IMEI, imei, sizeof(imei));
	modem_info_string_get(MODEM_INFO_ICCID, ccid, sizeof(ccid));

	LOG_INF("Modem IMEI: %s", imei);
	LOG_INF("Modem CCID: %s", ccid);

    return 0;
}

int modem_mod_connect(void) {
    int err;

    if (IS_ENABLED(CONFIG_DATE_TIME)) {
		/* Registering early for date_time event handler to avoid missing
		 * the first event after LTE is connected.
		 */
		date_time_register_handler(date_time_evt_handler);
	}

	// Connect to LTE
	LOG_INF("Connecting to LTE network. This may take a few minutes...");
	lte_lc_psm_req(true);
	lte_lc_func_mode_set(LTE_LC_FUNC_MODE_NORMAL);
	err = lte_lc_init_and_connect_async(lte_handler);
	if (err) {
		LOG_ERR("Connecting to network failed, err %d", err);
		return err;
	}

	// Wait for connection
	k_sem_take(&lte_connected, K_FOREVER);
	LOG_INF("Connected to LTE network!");

    /* A-GNSS/P-GPS needs to know the current time. */
	if (IS_ENABLED(CONFIG_DATE_TIME)) {
		printk("Waiting for current time\n");

		/* Wait for an event from the Date Time library. */
		k_sem_take(&time_update_finished, K_MINUTES(10));

		if (!date_time_is_valid()) {
			printk("Failed to get current time. Continuing anyway.\n");
		}
	}

    return 0;
}

int network_info_log(void)
{
    LOG_DBG("====== Cell Network Info ======");
    char sbuf[1024];
    modem_info_string_get(MODEM_INFO_RSRP, sbuf, sizeof(sbuf));
    LOG_DBG("Signal strength: %s", sbuf);
    modem_info_string_get(MODEM_INFO_CUR_BAND, sbuf, sizeof(sbuf));
    LOG_DBG("Current LTE band: %s", sbuf);
    modem_info_string_get(MODEM_INFO_SUP_BAND, sbuf, sizeof(sbuf));
    LOG_DBG("Supported LTE bands: %s", sbuf);
    modem_info_string_get(MODEM_INFO_AREA_CODE, sbuf, sizeof(sbuf));
    LOG_DBG("Tracking area code: %s", sbuf);
    modem_info_string_get(MODEM_INFO_UE_MODE, sbuf, sizeof(sbuf));
    LOG_DBG("Current mode: %s", sbuf);
    modem_info_string_get(MODEM_INFO_OPERATOR, sbuf, sizeof(sbuf));
    LOG_DBG("Current operator name: %s", sbuf);
    modem_info_string_get(MODEM_INFO_CELLID, sbuf, sizeof(sbuf));
    LOG_DBG("Cell ID of the device: %s", sbuf);
    modem_info_string_get(MODEM_INFO_IP_ADDRESS, sbuf, sizeof(sbuf));
    LOG_DBG("IP address of the device: %s", sbuf);
    modem_info_string_get(MODEM_INFO_FW_VERSION, sbuf, sizeof(sbuf));
    LOG_DBG("Modem firmware version: %s", sbuf);
    modem_info_string_get(MODEM_INFO_LTE_MODE, sbuf, sizeof(sbuf));
    LOG_DBG("LTE-M support mode: %s", sbuf);
    modem_info_string_get(MODEM_INFO_NBIOT_MODE, sbuf, sizeof(sbuf));
    LOG_DBG("NB-IoT support mode: %s", sbuf);
    modem_info_string_get(MODEM_INFO_GPS_MODE, sbuf, sizeof(sbuf));
    LOG_DBG("GPS support mode: %s", sbuf);
    modem_info_string_get(MODEM_INFO_DATE_TIME, sbuf, sizeof(sbuf));
    LOG_DBG("Mobile network time and date: %s", sbuf);
    LOG_DBG("===============================");
    return 0;
}

void init_certificates(void)
{
/////////////////
	// Check AWS credentials
	LOG_ERR("Getting certificate for SEC TAG %d", CONFIG_AWS_IOT_SEC_TAG);

	static char cred[4096];
	size_t cred_sz;
	bool cred_exists;
	int err;

	// Check for existance
	err =  modem_key_mgmt_exists(
		CONFIG_AWS_IOT_SEC_TAG,
		MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN,
		&cred_exists
	);
	if (err || cred_exists == false) {
		LOG_ERR("Failed to check CA certificate for SEC TAG %d with err %d", CONFIG_AWS_IOT_SEC_TAG, err);
	} else {
		LOG_INF("CA certificate exists");
	}

	err =  modem_key_mgmt_exists(
		CONFIG_AWS_IOT_SEC_TAG,
		MODEM_KEY_MGMT_CRED_TYPE_PRIVATE_CERT,
		&cred_exists
	);
	if (err || cred_exists == false) {
		LOG_ERR("Failed to check CA certificate for SEC TAG %d with err %d", CONFIG_AWS_IOT_SEC_TAG, err);
	} else {
		LOG_INF("private key exists");
	}

	err =  modem_key_mgmt_exists(
		CONFIG_AWS_IOT_SEC_TAG,
		MODEM_KEY_MGMT_CRED_TYPE_PUBLIC_CERT,
		&cred_exists
	);
	if (err || cred_exists == false) {
		LOG_ERR("Failed to check CA certificate for SEC TAG %d with err %d", CONFIG_AWS_IOT_SEC_TAG, err);
	} else {
		LOG_INF("public cert exists");
	}

	// Read certificates
	cred_sz = sizeof(cred);
	err = modem_key_mgmt_read(
		CONFIG_AWS_IOT_SEC_TAG,
		MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN,
		cred,
		&cred_sz
	);
	if (err) {
		LOG_ERR("Failed to get CA certificate for SEC TAG %d with err %d", CONFIG_AWS_IOT_SEC_TAG, err);
	} else {
		cred[cred_sz+1] = 0;
		LOG_INF("CA certificate %s", cred);
	}
	

	cred_sz = sizeof(cred);
	err = modem_key_mgmt_read(
		CONFIG_AWS_IOT_SEC_TAG,
		MODEM_KEY_MGMT_CRED_TYPE_PRIVATE_CERT,
		cred,
		&cred_sz
	);
	if (err) {
		LOG_ERR("Failed to get private key for SEC TAG %d with err %d", CONFIG_AWS_IOT_SEC_TAG, err);
	} else {
		cred[cred_sz+1] = 0;
		LOG_INF("private key %s", cred);
	}

	cred_sz = sizeof(cred);
	err = modem_key_mgmt_read(
		CONFIG_AWS_IOT_SEC_TAG,
		MODEM_KEY_MGMT_CRED_TYPE_PUBLIC_CERT,
		cred,
		&cred_sz
	);
	if (err) {
		LOG_ERR("Failed to get certificate for SEC TAG %d with err %d", CONFIG_AWS_IOT_SEC_TAG, err);
	} else {
		cred[cred_sz+1] = 0;
		LOG_INF("certificate %s", cred);
	}
}
