/*
 * Minimal Bluetooth Classic HID host (HIDP over L2CAP) for a single device.
 *
 * Zephyr has no HID host profile for BR/EDR, so the two HID channels are
 * handled directly on top of L2CAP:
 *   PSM 0x0011 - control   (GET_REPORT/SET_REPORT, handshakes)
 *   PSM 0x0013 - interrupt (input/output reports)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>

#include "hid_host.h"

LOG_MODULE_REGISTER(hid_host, LOG_LEVEL_INF);

#define HID_PSM_CTRL 0x0011
#define HID_PSM_INTR 0x0013

/* HIDP header: transaction type in the high nibble, parameter in the low one */
#define HIDP_HDR(type, param) (((type) << 4) | (param))
#define HIDP_HDR_TYPE(hdr)    ((hdr) >> 4)
#define HIDP_HDR_PARAM(hdr)   ((hdr) & 0x0f)

#define HIDP_TYPE_HANDSHAKE   0x0
#define HIDP_TYPE_HID_CONTROL 0x1
#define HIDP_TYPE_GET_REPORT  0x4
#define HIDP_TYPE_DATA        0xa

#define HIDP_HANDSHAKE_SUCCESSFUL       0x0
#define HIDP_CTRL_VIRTUAL_CABLE_UNPLUG  0x5

/* Only short control requests are sent, never output reports. */
#define HID_TX_MTU 16

NET_BUF_POOL_FIXED_DEFINE(hid_tx_pool, 2, BT_L2CAP_BUF_SIZE(HID_TX_MTU),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

static const struct hid_host_cb *host_cb;

static struct bt_l2cap_br_chan ctrl_chan;
static struct bt_l2cap_br_chan intr_chan;

static bool ctrl_up;
static bool intr_up;
/* The channels are opened by us, not by the device */
static bool initiator;
/* closed() is still owed to the application */
static bool session_open;

static const struct bt_l2cap_chan_ops ctrl_ops;
static const struct bt_l2cap_chan_ops intr_ops;

static struct bt_l2cap_chan *chan_prepare(struct bt_l2cap_br_chan *br_chan,
					  const struct bt_l2cap_chan_ops *ops)
{
	(void)memset(br_chan, 0, sizeof(*br_chan));
	br_chan->chan.ops = ops;
	br_chan->rx.mtu = BT_L2CAP_RX_MTU;
	br_chan->required_sec_level = BT_SECURITY_L2;

	return &br_chan->chan;
}

static void session_closed(void)
{
	if (!session_open) {
		return;
	}

	session_open = false;

	if (host_cb->closed) {
		host_cb->closed();
	}
}

static void ctrl_connected(struct bt_l2cap_chan *chan)
{
	int err;

	LOG_INF("HID control channel connected");
	ctrl_up = true;

	if (!initiator) {
		/* The device opens the interrupt channel on its own */
		return;
	}

	err = bt_l2cap_chan_connect(chan->conn, chan_prepare(&intr_chan, &intr_ops),
				    HID_PSM_INTR);
	if (err) {
		LOG_ERR("Failed to open HID interrupt channel (err %d)", err);
		bt_l2cap_chan_disconnect(chan);
	}
}

static void ctrl_disconnected(struct bt_l2cap_chan *chan)
{
	LOG_INF("HID control channel disconnected");
	ctrl_up = false;
	session_closed();
}

static int ctrl_recv(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
	uint8_t hdr;

	if (buf->len < 1) {
		return 0;
	}

	hdr = buf->data[0];

	switch (HIDP_HDR_TYPE(hdr)) {
	case HIDP_TYPE_HANDSHAKE:
		if (HIDP_HDR_PARAM(hdr) != HIDP_HANDSHAKE_SUCCESSFUL) {
			LOG_WRN("HIDP handshake error 0x%x", HIDP_HDR_PARAM(hdr));
		}
		break;
	case HIDP_TYPE_HID_CONTROL:
		if (HIDP_HDR_PARAM(hdr) == HIDP_CTRL_VIRTUAL_CABLE_UNPLUG) {
			LOG_INF("Device requested virtual cable unplug");
		}
		break;
	case HIDP_TYPE_DATA:
		/* Reply to GET_REPORT */
		if (buf->len >= 2) {
			LOG_INF("Got report type %u id 0x%02x (%u bytes)", HIDP_HDR_PARAM(hdr),
				buf->data[1], buf->len - 1);
		}
		break;
	default:
		LOG_DBG("Unhandled HIDP header 0x%02x", hdr);
		break;
	}

	return 0;
}

static void intr_connected(struct bt_l2cap_chan *chan)
{
	LOG_INF("HID interrupt channel connected");
	intr_up = true;

	if (ctrl_up && host_cb->ready) {
		host_cb->ready();
	}
}

static void intr_disconnected(struct bt_l2cap_chan *chan)
{
	LOG_INF("HID interrupt channel disconnected");
	intr_up = false;
	session_closed();
}

static int intr_recv(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
	if (buf->len < 2 ||
	    buf->data[0] != HIDP_HDR(HIDP_TYPE_DATA, HID_REPORT_TYPE_INPUT)) {
		return 0;
	}

	if (host_cb->input_report) {
		host_cb->input_report(&buf->data[1], buf->len - 1);
	}

	return 0;
}

static const struct bt_l2cap_chan_ops ctrl_ops = {
	.connected = ctrl_connected,
	.disconnected = ctrl_disconnected,
	.recv = ctrl_recv,
};

static const struct bt_l2cap_chan_ops intr_ops = {
	.connected = intr_connected,
	.disconnected = intr_disconnected,
	.recv = intr_recv,
};

static int hid_accept(struct bt_conn *conn, struct bt_l2cap_server *server,
		      struct bt_l2cap_chan **chan)
{
	if (server->psm == HID_PSM_CTRL) {
		if (!hid_host_is_idle()) {
			return -ENOMEM;
		}

		initiator = false;
		session_open = true;
		*chan = chan_prepare(&ctrl_chan, &ctrl_ops);

		return 0;
	}

	/* The interrupt channel has to follow control on the same ACL link */
	if (intr_chan.chan.conn || ctrl_chan.chan.conn != conn) {
		return -ENOMEM;
	}

	*chan = chan_prepare(&intr_chan, &intr_ops);

	return 0;
}

static struct bt_l2cap_server ctrl_server = {
	.psm = HID_PSM_CTRL,
	.sec_level = BT_SECURITY_L2,
	.accept = hid_accept,
};

static struct bt_l2cap_server intr_server = {
	.psm = HID_PSM_INTR,
	.sec_level = BT_SECURITY_L2,
	.accept = hid_accept,
};

int hid_host_init(const struct hid_host_cb *cb)
{
	int err;

	if (!cb) {
		return -EINVAL;
	}

	host_cb = cb;

	err = bt_l2cap_br_server_register(&ctrl_server);
	if (err) {
		return err;
	}

	return bt_l2cap_br_server_register(&intr_server);
}

bool hid_host_is_idle(void)
{
	return !ctrl_chan.chan.conn && !intr_chan.chan.conn;
}

int hid_host_connect(struct bt_conn *conn)
{
	int err;

	if (!hid_host_is_idle()) {
		return -EBUSY;
	}

	initiator = true;
	session_open = true;

	err = bt_l2cap_chan_connect(conn, chan_prepare(&ctrl_chan, &ctrl_ops), HID_PSM_CTRL);
	if (err) {
		session_open = false;
	}

	return err;
}

int hid_host_get_report(uint8_t type, uint8_t id)
{
	struct net_buf *buf;
	int err;

	if (!ctrl_up) {
		return -ENOTCONN;
	}

	buf = net_buf_alloc(&hid_tx_pool, K_NO_WAIT);
	if (!buf) {
		return -ENOBUFS;
	}

	net_buf_reserve(buf, BT_L2CAP_CHAN_SEND_RESERVE);
	net_buf_add_u8(buf, HIDP_HDR(HIDP_TYPE_GET_REPORT, type));
	net_buf_add_u8(buf, id);

	err = bt_l2cap_chan_send(&ctrl_chan.chan, buf);
	if (err < 0) {
		net_buf_unref(buf);
		return err;
	}

	return 0;
}
