/*
 * Bluetooth Classic host for a DualSense / DualSense Edge controller.
 *
 * - First use: put the controller in pairing mode (hold CREATE + PS until the
 *   light bar blinks). It is found by inquiry, paired and bonded.
 * - Later on: press PS, the bonded controller connects back on its own.
 *
 * Every button press/release is printed on the console.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/assigned_numbers.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/classic/classic.h>

#include "dualsense.h"
#include "hid_host.h"
#include "link.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/*
 * An inquiry keeps the radio busy and leaves page scan little air time, while
 * a bonded controller only pages us for a few seconds after a PS press. Hence
 * the inquiry runs in short bursts, with the air left clear for incoming
 * connections in between.
 */
/* Inquiry length in 1.28 s units */
#define INQUIRY_LENGTH        3
/* Past the end of the inquiry, to also cut the name requests following it */
#define INQUIRY_DURATION      K_MSEC(INQUIRY_LENGTH * 1280 + 200)
#define INQUIRY_PAUSE         K_SECONDS(5)
#define SCAN_RETRY_DELAY      K_SECONDS(1)
#define DISCOVERY_RESULTS_MAX 8

/* Let the stack finish the post-connection feature exchange first */
#define HID_OPEN_DELAY          K_MSEC(250)
/*
 * A reconnecting controller opens the HID channels itself, only step in when
 * it did not.
 */
#define HID_OPEN_FALLBACK_DELAY K_SECONDS(3)

/* Only the low nibble of the peripheral minor class is the device type */
#define COD_PERIPHERAL_DEVICE_TYPE_MASK 0x0f

static struct bt_br_discovery_result discovery_results[DISCOVERY_RESULTS_MAX];

static struct bt_conn *pad_conn;

/* Outgoing connection to 'candidate' is in progress */
static atomic_t connecting;
static bt_addr_t candidate;

static bool inquiry_active;

static void scan_work_handler(struct k_work *work);
static void connect_work_handler(struct k_work *work);
static void hid_open_work_handler(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(scan_work, scan_work_handler);
static K_WORK_DEFINE(connect_work, connect_work_handler);
static K_WORK_DELAYABLE_DEFINE(hid_open_work, hid_open_work_handler);

static bool cod_is_gamepad(const uint8_t cod[3])
{
	return BT_COD_MAJOR_DEVICE_CLASS(cod) == BT_COD_MAJOR_DEVICE_CLASS_PERIPHERAL &&
	       (BT_COD_MINOR_DEVICE_CLASS(cod) & COD_PERIPHERAL_DEVICE_TYPE_MASK) ==
		       BT_COD_MINOR_DEVICE_CLASS_PERIPHERAL_DEVICE_TYPE_GAMEPAD;
}

static void inquiry_stop(void)
{
	/* Cancels the pending remote name requests as well */
	bt_br_discovery_stop();
	inquiry_active = false;
}

/* Alternates between an inquiry burst and a page scan only pause */
static void scan_work_handler(struct k_work *work)
{
	const struct bt_br_discovery_param param = {
		.length = INQUIRY_LENGTH,
		.limited = false,
	};
	int err;

	if (pad_conn || atomic_get(&connecting)) {
		return;
	}

	if (inquiry_active) {
		inquiry_stop();
		k_work_reschedule(&scan_work, INQUIRY_PAUSE);
		return;
	}

	err = bt_br_discovery_start(&param, discovery_results, ARRAY_SIZE(discovery_results));
	if (err && err != -EALREADY) {
		LOG_ERR("Failed to start discovery (err %d)", err);
		k_work_reschedule(&scan_work, INQUIRY_PAUSE);
		return;
	}

	inquiry_active = true;
	k_work_reschedule(&scan_work, INQUIRY_DURATION);
}

static void discovery_recv(const struct bt_br_discovery_result *result)
{
	if (pad_conn || !cod_is_gamepad(result->cod)) {
		return;
	}

	/* The same device is reported several times (RSSI, EIR, name) */
	if (!atomic_cas(&connecting, 0, 1)) {
		return;
	}

	bt_addr_copy(&candidate, &result->addr);
	k_work_submit(&connect_work);
}

static struct bt_br_discovery_cb discovery_cb = {
	.recv = discovery_recv,
};

static void connect_work_handler(struct k_work *work)
{
	char addr[BT_ADDR_STR_LEN];
	struct bt_conn *conn;

	bt_addr_to_str(&candidate, addr, sizeof(addr));
	LOG_INF("Found controller %s, connecting", addr);

	inquiry_stop();

	/*
	 * A controller in pairing mode has dropped its old link key, so a bond
	 * we may still hold for it would only make the authentication fail.
	 * A bond with another controller has to go as well: a single controller
	 * is served and the link key pool (CONFIG_BT_MAX_PAIRED) has one slot.
	 */
	bt_br_unpair(BT_ADDR_ANY);

	conn = bt_conn_create_br(&candidate, BT_BR_CONN_PARAM_DEFAULT);
	if (!conn) {
		LOG_ERR("Failed to create connection to %s", addr);
		atomic_clear(&connecting);
		k_work_reschedule(&scan_work, SCAN_RETRY_DELAY);
		return;
	}

	/* The reference is taken in connected() for both connection directions */
	bt_conn_unref(conn);
}

static void hid_open_work_handler(struct k_work *work)
{
	struct bt_conn *conn;
	int err;

	if (!pad_conn || !hid_host_is_idle()) {
		return;
	}

	conn = bt_conn_ref(pad_conn);

	err = hid_host_connect(conn);
	if (err) {
		LOG_ERR("Failed to open HID channels (err %d)", err);
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}

	bt_conn_unref(conn);
}

static enum bt_br_conn_req_rsp conn_request(const bt_addr_t *addr, uint32_t cod)
{
	char addr_str[BT_ADDR_STR_LEN];

	bt_addr_to_str(addr, addr_str, sizeof(addr_str));
	LOG_INF("Connection request from %s (CoD 0x%06x)", addr_str, cod);

	return BT_BR_CONN_REQ_ACCEPT_PERIPHERAL;
}

/*
 * Page scan lets a bonded controller reconnect with the PS button. It is only
 * needed while no controller is connected, and would take air time from the
 * active link otherwise.
 */
static int page_scan_enable(bool enable)
{
	int err;

	err = bt_br_set_connectable(enable, conn_request);
	if (err == -EALREADY) {
		return 0;
	}

	if (err) {
		LOG_ERR("Failed to %s page scan (err %d)", enable ? "enable" : "disable", err);
	}

	return err;
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	const bt_addr_t *dst = bt_conn_get_dst_br(conn);
	char addr[BT_ADDR_STR_LEN];
	bool outgoing;

	if (!dst) {
		/* Not a BR/EDR connection */
		return;
	}

	bt_addr_to_str(dst, addr, sizeof(addr));

	outgoing = atomic_get(&connecting) && bt_addr_eq(dst, &candidate);
	if (outgoing) {
		atomic_clear(&connecting);
	}

	if (err) {
		LOG_WRN("Connection to %s failed (err 0x%02x)", addr, err);
		if (outgoing) {
			k_work_reschedule(&scan_work, SCAN_RETRY_DELAY);
		}
		return;
	}

	if (pad_conn) {
		LOG_WRN("Already serving a controller, dropping %s", addr);
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		return;
	}

	LOG_INF("Connected to %s (%s)", addr, outgoing ? "outgoing" : "incoming");

	pad_conn = bt_conn_ref(conn);
	dualsense_reset();

	/* An incoming connection may arrive in the middle of an inquiry */
	inquiry_stop();
	page_scan_enable(false);

	k_work_reschedule(&hid_open_work, outgoing ? HID_OPEN_DELAY : HID_OPEN_FALLBACK_DELAY);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (conn != pad_conn) {
		return;
	}

	LOG_INF("Controller disconnected (reason 0x%02x)", reason);

	k_work_cancel_delayable(&hid_open_work);

	bt_conn_unref(pad_conn);
	pad_conn = NULL;
	dualsense_reset();

	page_scan_enable(true);

	/* The controller is bonded now, so expect it to connect back first */
	k_work_reschedule(&scan_work, INQUIRY_PAUSE);
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	if (conn != pad_conn) {
		return;
	}

	if (err) {
		LOG_WRN("Security failed (level %d, err %d)", level, err);
		return;
	}

	LOG_INF("Link secured (level %d)", level);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

static void hid_ready(void)
{
	int err;

	LOG_INF("Controller ready, waiting for button presses");

	err = dualsense_enable_full_report();
	if (err) {
		LOG_WRN("Failed to request the full input report (err %d)", err);
	}
}

static void hid_closed(void)
{
	/* HID is all the link is used for */
	if (pad_conn) {
		bt_conn_disconnect(pad_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

static const struct hid_host_cb hid_callbacks = {
	.ready = hid_ready,
	.input_report = dualsense_handle_input_report,
	.closed = hid_closed,
};

int main(void)
{
	int err;

	link_init();

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return 0;
	}

	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
		settings_load();
	}

	err = hid_host_init(&hid_callbacks);
	if (err) {
		LOG_ERR("HID host init failed (err %d)", err);
		return 0;
	}

	err = page_scan_enable(true);
	if (err) {
		return 0;
	}

	/*
	 * A reconnecting controller pages for a few seconds only, the default
	 * 11 ms window every 1.28 s is easy to miss.
	 */
	err = bt_br_page_scan_update_param(BT_BR_PAGE_SCAN_PARAM_FAST_R1);
	if (err) {
		LOG_WRN("Failed to speed up page scan (err %d)", err);
	}

	bt_br_discovery_cb_register(&discovery_cb);

	LOG_INF("Hold CREATE + PS on the controller until the light bar blinks to pair,");
	LOG_INF("or press PS on an already paired controller");

	k_work_reschedule(&scan_work, K_NO_WAIT);

	return 0;
}
