// SPDX-License-Identifier: GPL-2.0-only
/*
 * Touch driver for the Cypress CY8CTMG120 on the RoomWizard, bound to the
 * vendor DT node `tsc_panjit@03` (compatible "panjit_ts") that our image
 * keeps unchanged.
 *
 * Adapted from drivers/input/touchscreen/cy8ctmg110_ts.c (Samuli Konttila,
 * Aava Mobile), whose register map this part shares byte for byte
 * (SYSTEM_ANALYSIS.md#33-touch). The host handshake is NOT that driver's:
 * it is the vendor kernel's panjit_ts, read out of its disassembly —
 *   - probe: reset pulse; reg0 = 0x00; clear reg1 until it reads 0; then,
 *     with the IRQ requested, reg0 = 0x08 (the base driver's 0x10 wedges
 *     the part, measured);
 *   - the IRQ is level-low, whatever the node's falling-edge cell says;
 *   - while a finger is down the host polls every 10 ms and writes
 *     reg1 = 0x00 after each read. Without that write the part reports one
 *     touch and then stalls (measured);
 *   - on lift, reg0 = 0x08 and reg1 = 0x00 again, then the IRQ re-arms.
 * Coordinates are 12-bit 0..4095 with flag bits above, masked 0x0fff.
 *
 * Event order per frame is ABS_X, ABS_Y, BTN_TOUCH, SYN_REPORT, the order
 * native_apps/common/touch_input.c relies on. The input device is named
 * "panjit_ts" because userspace matches that name.
 *
 * Both fingers also go out as type-B MT slots (slot 0 = X1/Y1, slot 1 =
 * X2/Y2, valid while byte 8's count covers them), ahead of the legacy
 * events. The slots are reported by hand: input_mt_sync_frame()'s pointer
 * emulation emits BTN_TOUCH before ABS_X/ABS_Y, which breaks that order.
 * No ABS_PRESSURE: the vendor's was a constant 255/0 and nothing reads it.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/of.h>

#define DRIVER_NAME	"panjit_ts"

#define REG_MODE	0
#define REG_ACK		1
#define REG_X1		3	/* X1 Y1 X2 Y2 big-endian 16-bit, then fingers */
#define BURST_LEN	9
#define MODE_RUN	0x08
#define COORD_NONE	0xffff
#define COORD_MASK	0x0fff
#define COORD_MAX	4095
#define POLL_MS		10
#define MAX_FINGERS	2

static bool debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "log every burst as raw bytes (rate-limited)");

struct cy8_ts {
	struct i2c_client *client;
	struct input_dev *input;
	char phys[32];
};

static int cy8_read(struct cy8_ts *ts, u8 reg, u8 *buf, u8 len)
{
	struct i2c_msg msg[2] = {
		{ .addr = ts->client->addr, .len = 1, .buf = &reg },
		{ .addr = ts->client->addr, .flags = I2C_M_RD, .len = len, .buf = buf },
	};
	int ret = i2c_transfer(ts->client->adapter, msg, 2);

	if (ret == 2)
		return 0;
	return ret < 0 ? ret : -EIO;
}

static int cy8_write(struct cy8_ts *ts, u8 reg, u8 val)
{
	u8 w[2] = { reg, val };
	int ret = i2c_master_send(ts->client, w, sizeof(w));

	if (ret == sizeof(w))
		return 0;
	return ret < 0 ? ret : -EIO;
}

/* Poll until the finger lifts; the IRQ stays masked (ONESHOT) meanwhile. */
static irqreturn_t cy8_irq_thread(int irq, void *dev_id)
{
	struct cy8_ts *ts = dev_id;
	struct input_dev *input = ts->input;
	bool down = false;
	u8 b[BURST_LEN];

	int i;

	for (;;) {
		int x[MAX_FINGERS], y[MAX_FINGERS];
		bool on[MAX_FINGERS];

		if (cy8_read(ts, REG_X1, b, sizeof(b))) {
			dev_err_ratelimited(&ts->client->dev, "burst read failed\n");
			break;
		}
		if (debug)
			dev_info_ratelimited(&ts->client->dev, "%*ph\n", BURST_LEN, b);
		if (!b[8])
			break;
		cy8_write(ts, REG_ACK, 0);

		for (i = 0; i < MAX_FINGERS; i++) {
			x[i] = (b[4 * i] << 8) | b[4 * i + 1];
			y[i] = (b[4 * i + 2] << 8) | b[4 * i + 3];
			on[i] = i < b[8] && x[i] != COORD_NONE && y[i] != COORD_NONE;
			x[i] &= COORD_MASK;
			y[i] &= COORD_MASK;

			input_mt_slot(input, i);
			input_mt_report_slot_state(input, MT_TOOL_FINGER, on[i]);
			if (on[i]) {
				input_report_abs(input, ABS_MT_POSITION_X, x[i]);
				input_report_abs(input, ABS_MT_POSITION_Y, y[i]);
			}
		}
		if (on[0]) {
			input_report_abs(input, ABS_X, x[0]);
			input_report_abs(input, ABS_Y, y[0]);
			input_report_key(input, BTN_TOUCH, 1);
			down = true;
		}
		input_sync(input);
		msleep(POLL_MS);
	}

	cy8_write(ts, REG_MODE, MODE_RUN);
	cy8_write(ts, REG_ACK, 0);
	for (i = 0; i < MAX_FINGERS; i++) {
		input_mt_slot(input, i);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, false);
	}
	if (down)
		input_report_key(input, BTN_TOUCH, 0);
	input_sync(input);
	return IRQ_HANDLED;
}

static int cy8_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct gpio_desc *reset;
	struct cy8_ts *ts;
	u8 ack = 0xff;
	int err, i;

	if (client->irq <= 0) {
		dev_err(dev, "no IRQ in the DT node\n");
		return -EINVAL;
	}

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;
	ts->client = client;

	/* reset-gpios is active-high; pulse it so a reload resets the part too. */
	reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(reset))
		return PTR_ERR(reset);
	if (reset) {
		msleep(20);
		gpiod_set_value_cansleep(reset, 0);
		msleep(100);
	}

	cy8_write(ts, REG_MODE, 0);
	for (i = 0; i < 10; i++) {
		err = cy8_read(ts, REG_ACK, &ack, 1);
		if (!err && !ack)
			break;
		cy8_write(ts, REG_ACK, 0);
	}
	if (err) {
		dev_err(dev, "controller does not answer: %d\n", err);
		return -ENXIO;
	}

	ts->input = devm_input_allocate_device(dev);
	if (!ts->input)
		return -ENOMEM;

	snprintf(ts->phys, sizeof(ts->phys), "%s/input0", dev_name(dev));
	ts->input->name = DRIVER_NAME;
	ts->input->phys = ts->phys;
	ts->input->id.bustype = BUS_I2C;

	input_set_capability(ts->input, EV_KEY, BTN_TOUCH);
	input_set_abs_params(ts->input, ABS_X, 0, COORD_MAX, 0, 0);
	input_set_abs_params(ts->input, ABS_Y, 0, COORD_MAX, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_POSITION_X, 0, COORD_MAX, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_POSITION_Y, 0, COORD_MAX, 0, 0);
	err = input_mt_init_slots(ts->input, MAX_FINGERS, INPUT_MT_DIRECT);
	if (err)
		return err;

	err = input_register_device(ts->input);
	if (err)
		return err;

	err = devm_request_threaded_irq(dev, client->irq, NULL, cy8_irq_thread,
					IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					DRIVER_NAME, ts);
	if (err) {
		dev_err(dev, "request irq %d: %d\n", client->irq, err);
		return err;
	}

	cy8_write(ts, REG_MODE, MODE_RUN);
	i2c_set_clientdata(client, ts);
	dev_info(dev, "irq %d, reset %s, reg1 cleared after %d reads\n",
		 client->irq, reset ? "gpio" : "none", i + 1);
	return 0;
}

static const struct i2c_device_id cy8_idtable[] = {
	{ DRIVER_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cy8_idtable);

static const struct of_device_id cy8_of_match[] = {
	{ .compatible = "panjit_ts" },
	{ }
};
MODULE_DEVICE_TABLE(of, cy8_of_match);

static struct i2c_driver cy8_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = cy8_of_match,
	},
	.id_table = cy8_idtable,
	.probe = cy8_probe,
};
module_i2c_driver(cy8_driver);

MODULE_DESCRIPTION("CY8CTMG120 touchscreen on the RoomWizard");
MODULE_LICENSE("GPL v2");
