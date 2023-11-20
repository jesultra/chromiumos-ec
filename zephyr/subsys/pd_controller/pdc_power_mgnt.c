/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TCPMv3
 */
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/pdc.h>

LOG_MODULE_DECLARE(pdc, LOG_LEVEL_INF);

#define NODE_ID	DT_NODELABEL(pdc_power_p0p1)
#if DT_NODE_HAS_STATUS(NODE_ID, okay)
#define PDC0	NODE_ID
#else
#error "Can't find PDC Node"
#endif


static void create_thread(const struct device *dev);

struct tcpm_data_t {
	/** This port's thread */
	k_tid_t thread;
	/** This port thread's data */
	struct k_thread thread_data;
	const struct device *dev;
	const struct device *pdc[2];
};

struct tcpm_config_t {
	/**
	 * The usbc stack initializes this pointer that creates the
	 * main thread for this port
	 */
	void (*create_thread)(const struct device *dev);
};

K_THREAD_STACK_DEFINE(tcpm_stack_area, 2000);

static struct tcpm_data_t tcpm_data = {
	.pdc[0] = DEVICE_DT_GET(PDC0),
};

static const struct tcpm_config_t tcpm_config = {
	.create_thread = create_thread,
};

/*
 * Some TEST code to access the PDC driver
 */
static void run_tcpm(void *dev, void *unused1, void *unused2)
{
	struct tcpm_data_t *data = ((const struct device *)dev)->data;
	int rv;

	int k = 0;
	union notification_enable_t bits;
	struct device_capability_t caps;
	union connector_capability_t ccaps;
	uint16_t vb;
	uint32_t fwv;
	int delay = 0;

	caps.bNumPorts = 0;

	while (1) {
		/* Let Zephyr logging stabilize before starting the PDC */
		/* This is only added so the PDC logs are easier to read */
		if (delay != 5) {
			delay++;
			printk("DELAY: %d\n", delay);
		}

		if (delay == 5) {
			switch (k) {
			case 0:
				/* Enable PDC */
				pdc_enable(data->pdc[0]);
				k++;
				break;
			case 1:
				/* Set notifications */
				bits.raw_value = 0xDBE7; //0xffff;
				pdc_set_notification_enable(data->pdc[0], bits, 0);
				k++;
				break;
			case 2:
				/* Reset PDC */
				pdc_reset(data->pdc[0]);
				k++;
				break;
			case 3:
				/* Set CC Operation mode */
				pdc_set_ccom(data->pdc[0], CCOM_DRP, DRP_NORMAL);
				k++;
				break;
			case 4:
				/* Call Intel Specific command for RVP. REMOVE FOR OTHER BOARDS */
				pdc_set_voltage(data->pdc[0]);
				k++;
				break;
			case 5:
				/* Get device capabilities */
				pdc_get_capability(data->pdc[0], &caps);
				k++;
				break;
			case 6:
				/* Analyze device caps */
				printk("\n\n***PNUM: %d\n", caps.bNumPorts);
				k++;
				break;
			case 7:
				/* Get Connector caps */
				pdc_get_connector_capability(data->pdc[0], &ccaps);
				k++;
				break;
			case 8:
				/* Analyze connector caps */
				printk("\n\n***CCAPS: %04x\n", ccaps.raw_value);
				k = 10;
				break;
			case 9:
				vb = 0;
				rv = pdc_getvbus_voltage(data->pdc[0], &vb);
				printk("V(%d): %04x\n", rv, vb);
				k = 11;
				break;
			case 10:
				vb = 0;
				rv = pdc_get_fw_version(data->pdc[0], &fwv);
				printk("TEST(%d): %04x\n", rv, fwv);
				k = 9;
				break;
			case 11:
				break;
			}
		}

		k_sleep(K_MSEC(1000));
	}
}

/**
 * @brief Initialize the USB-C Subsystem
 */
static int tcpm_subsys_init(const struct device *dev)
{
	struct tcpm_data_t *data = dev->data;
	const struct tcpm_config_t *const cfg = dev->config;

	/* Make sure TCPC is ready */
	if (!device_is_ready(data->pdc[0])) {
		LOG_ERR("PDC NOT READY\n");
		return -ENODEV;
	}

	/* Create the thread for this port */
	cfg->create_thread(dev);
	data->dev = dev;

	LOG_INF("\n\nTCPMv3 Started\n");
	return 0;
}

static void create_thread(const struct device *dev)
{
	struct tcpm_data_t *data = dev->data;

	printk("CREATE THREAD\n");
	data->thread = k_thread_create(
		&data->thread_data, tcpm_stack_area,
		K_THREAD_STACK_SIZEOF(tcpm_stack_area), run_tcpm, (void *)dev,
		0, 0, 8, K_ESSENTIAL, K_NO_WAIT);
}

DEVICE_DEFINE(pdc_power_p0p1, "pdc_power_p0p1", &tcpm_subsys_init, NULL, &tcpm_data,
			&tcpm_config, POST_KERNEL,
			CONFIG_APPLICATION_INIT_PRIORITY, NULL);
