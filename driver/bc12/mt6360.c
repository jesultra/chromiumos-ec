#include "charger.h"
#include "charge_manager.h"
#include "console.h"
#include "crc8.h"
#include "driver/bc12/mt6360.h"
#include "ec_commands.h"
#include "hooks.h"
#include "i2c.h"
#include "task.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "util.h"

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)
#define CPRINTS(format, args...) \
	cprints(CC_CHARGER, "%s " format, "MT6360", ## args)

static enum ec_error_list mt6360_read8(int reg, int *val)
{
	return i2c_read8(mt6360_config.i2c_port, mt6360_config.i2c_addr_flags,
			reg, val);
}

static enum ec_error_list mt6360_write8(int reg, int val)
{
	return i2c_write8(mt6360_config.i2c_port, mt6360_config.i2c_addr_flags,
			reg, val);
}

static int mt6360_update_bits(int reg, int mask, int val)
{
	int rv;
	int reg_val = 0;

	rv = mt6360_read8(reg, &reg_val);
	if (rv)
		return rv;
	reg_val &= ~mask;
	reg_val |= (mask & val);
	rv = mt6360_write8(reg, reg_val);
	return rv;
}

static inline int mt6360_set_bit(int reg, int mask)
{
	return mt6360_update_bits(reg, mask, mask);
}

static inline int mt6360_clr_bit(int reg, int mask)
{
	return mt6360_update_bits(reg, mask, 0x00);
}


static int mt6360_get_bc12_device_type(void)
{
	int reg;

	if (mt6360_read8(MT6360_REG_USB_STATUS_1, &reg))
		return CHARGE_SUPPLIER_NONE;

	switch (reg & MT6360_MASK_USB_STATUS) {
	case MT6360_MASK_SDP:
		CPRINTS("BC12 SDP");
		return CHARGE_SUPPLIER_BC12_SDP;
	case MT6360_MASK_CDP:
		CPRINTS("BC12 CDP");
		return CHARGE_SUPPLIER_BC12_CDP;
	case MT6360_MASK_DCP:
		CPRINTS("BC12 DCP");
		return CHARGE_SUPPLIER_BC12_DCP;
	default:
		CPRINTS("BC12 NONE");
		return CHARGE_SUPPLIER_NONE;
	}
}

static int mt6360_get_bc12_ilim(int charge_supplier)
{
	switch (charge_supplier) {
	case CHARGE_SUPPLIER_BC12_DCP:
	case CHARGE_SUPPLIER_BC12_CDP:
		return USB_CHARGER_MAX_CURR_MA;
	case CHARGE_SUPPLIER_BC12_SDP:
	default:
		return USB_CHARGER_MIN_CURR_MA;
	}
}

static int mt6360_enable_bc12_detection(int en)
{
	int rv;

	if (en) {
#ifdef CONFIG_MT6360_BC12_DETECT_GPIO
		gpio_set_level(CONFIG_MT6360_BC12_DETECT_GPIO, 1);
#endif
		return mt6360_set_bit(MT6360_REG_DEVICE_TYPE,
				      MT6360_MASK_USBCHGEN);
	}

	rv = mt6360_clr_bit(MT6360_REG_DEVICE_TYPE, MT6360_MASK_USBCHGEN);
#ifdef CONFIG_MT6360_BC12_DETECT_GPIO
	gpio_set_level(CONFIG_MT6360_BC12_DETECT_GPIO, 0);
#endif
	return rv;
}

static int mt6360_ramp_allowed(int supplier)
{
	return supplier == CHARGE_SUPPLIER_BC12_DCP;
}

static int mt6360_ramp_max(int supplier, int sup_curr)
{
	return mt6360_get_bc12_ilim(supplier);
}

static void mt6360_usb_charger_task(const int port)
{
	int current_bc12_type = CHARGE_SUPPLIER_NONE;

	while (1) {
		int new_bc12_type = CHARGE_SUPPLIER_NONE;

		task_wait_event(-1);

		/* Run bc12 detection only if port is connected and is sink */
		if (pd_snk_is_vbus_provided(port)) {
			mt6360_enable_bc12_detection(1);
			/* TODO: change this to interrupt */
			usleep(300 * MSEC);
			new_bc12_type = mt6360_get_bc12_device_type();
			mt6360_enable_bc12_detection(0);
		}

		if (current_bc12_type == new_bc12_type)
			/* skip update if type doesn't change */
			continue;

		if (new_bc12_type == CHARGE_SUPPLIER_NONE) {
			CPRINTS("VBUS detached");
			charge_manager_update_charge(
					current_bc12_type, 0, NULL);
		} else {
			struct charge_port_info chg = {
				.current = mt6360_get_bc12_ilim(new_bc12_type),
				.voltage = USB_CHARGER_VOLTAGE_MV,
			};

			charge_manager_update_charge(new_bc12_type, 0, &chg);
		}

		current_bc12_type = new_bc12_type;
	}
}

/* LDO */
/*
 * TODO(pihsun): Should these functions be here or in asurada/ ?
 */
static int mt6360_ldo_write8(int reg, int val)
{
	/*
	 * TODO(pihsun): The checksum from I2C_FLAG_PEC happens to be correct
	 * because the length == 1 -> the high 3 bits of the offset byte is 0.
	 * Consider moving the checksum to here for a general function that can
	 * handle 1~4 bytes of data?
	 */
	return i2c_write8(mt6360_config.i2c_port,
			  MT6360_LDO_SLAVE_ADDR_FLAGS | I2C_FLAG_PEC, reg, val);
}

static int mt6360_ldo_read8(int reg, int *val)
{
	int rv;
	uint8_t crc = 0, real_crc;
	uint8_t addr = MT6360_LDO_SLAVE_ADDR_FLAGS;
	uint8_t out[3] = {(addr << 1) | 1, reg};

	rv = i2c_read16(mt6360_config.i2c_port, addr, reg, val);
	if (rv)
		return rv;

	real_crc = (*val >> 8) & 0xFF;
	*val &= 0xFF;
	out[2] = *val;
	crc = crc8(out, ARRAY_SIZE(out));

	if (crc != real_crc)
		return EC_ERROR_CRC;

	return EC_SUCCESS;
}

static int mt6360_ldo_update_bits(int reg, int mask, int val)
{
	int rv;
	int reg_val = 0;

	rv = mt6360_ldo_read8(reg, &reg_val);
	if (rv)
		return rv;
	reg_val &= ~mask;
	reg_val |= (mask & val);
	rv = mt6360_ldo_write8(reg, reg_val);
	return rv;
}

const uint32_t MT6360_LDO3_VOSEL_TABLE[16] = {
	[0x4] = 1800000,
	[0xA] = 2900000,
	[0xB] = 3000000,
	[0xD] = 3300000,
};

const uint32_t MT6360_LDO5_VOSEL_TABLE[8] = {
	[0x2] = 2900000,
	[0x3] = 3000000,
	[0x5] = 3300000,
};

/*
 * TODO(pihsun): Remove duplicate code for LDO3 & LDO5
 */
int mt6360_ldo_get_info(enum mt6360_ldo_id ldo_id, char *name,
			uint32_t *num_voltages, uint32_t *voltages_uV)
{
	int i;
	int cnt = 0;

	switch (ldo_id) {
	case MT6360_LDO3:
		strzcpy(name, "mt6360_ldo3", MAX_EC_REGULATOR_NAME_LEN);
		for (i = 0; i < ARRAY_SIZE(MT6360_LDO3_VOSEL_TABLE); i++) {
			int uV = MT6360_LDO3_VOSEL_TABLE[i];
			if (!uV)
				continue;
			if (cnt < MAX_EC_REGULATOR_VOLTAGE_COUNT) {
				voltages_uV[cnt++] = uV;
			} else {
				CPRINTS("LDO3 Voltage info overflow: %d", uV);
			}
		}
		*num_voltages = cnt;
		return EC_SUCCESS;
	case MT6360_LDO5:
		strzcpy(name, "mt6360_ldo5", MAX_EC_REGULATOR_NAME_LEN);
		for (i = 0; i < ARRAY_SIZE(MT6360_LDO5_VOSEL_TABLE); i++) {
			int uV = MT6360_LDO5_VOSEL_TABLE[i];
			if (!uV)
				continue;
			if (cnt < MAX_EC_REGULATOR_VOLTAGE_COUNT) {
				voltages_uV[cnt++] = uV;
			} else {
				CPRINTS("LDO5 Voltage info overflow: %d", uV);
			}
		}
		*num_voltages = cnt;
		return EC_SUCCESS;
	default:
		return EC_ERROR_INVAL;
	}
}

int mt6360_ldo_enable(enum mt6360_ldo_id ldo_id, uint8_t enable)
{
	switch (ldo_id) {
	case MT6360_LDO3:
		if (enable)
			return mt6360_ldo_update_bits(
				MT6360_REG_LDO3_EN_CTRL2,
				MT6360_MASK_LDO3_SW_OP_EN |
					MT6360_MASK_LDO3_SW_EN,
				MT6360_MASK_LDO3_SW_OP_EN |
					MT6360_MASK_LDO3_SW_EN);
		else
			return mt6360_ldo_update_bits(
				MT6360_REG_LDO3_EN_CTRL2,
				MT6360_MASK_LDO3_SW_OP_EN |
					MT6360_MASK_LDO3_SW_EN,
				MT6360_MASK_LDO3_SW_OP_EN);
	case MT6360_LDO5:
		if (enable)
			return mt6360_ldo_update_bits(
				MT6360_REG_LDO5_EN_CTRL2,
				MT6360_MASK_LDO5_SW_OP_EN |
					MT6360_MASK_LDO5_SW_EN,
				MT6360_MASK_LDO5_SW_OP_EN |
					MT6360_MASK_LDO5_SW_EN);
		else
			return mt6360_ldo_update_bits(
				MT6360_REG_LDO5_EN_CTRL2,
				MT6360_MASK_LDO5_SW_OP_EN |
					MT6360_MASK_LDO5_SW_EN,
				MT6360_MASK_LDO5_SW_OP_EN);
	default:
		return EC_ERROR_INVAL;
	}
}

int mt6360_ldo_is_enabled(enum mt6360_ldo_id ldo_id, uint8_t *enabled)
{
	int rv;
	int data;

	switch (ldo_id) {
	case MT6360_LDO3:
		rv = mt6360_ldo_read8(MT6360_REG_LDO3_EN_CTRL2, &data);
		if (rv) {
			CPRINTS("Error reading LDO3 enabled: %d", rv);
			return rv;
		}
		*enabled = !!(data & MT6360_MASK_LDO3_SW_EN);
		return EC_SUCCESS;
	case MT6360_LDO5:
		rv = mt6360_ldo_read8(MT6360_REG_LDO5_EN_CTRL2, &data);
		if (rv) {
			CPRINTS("Error reading LDO5 enabled: %d", rv);
			return rv;
		}
		*enabled = !!(data & MT6360_MASK_LDO5_SW_EN);
		return EC_SUCCESS;
	default:
		return EC_ERROR_INVAL;
	}
}

int mt6360_ldo_set_voltage(enum mt6360_ldo_id ldo_id, int min_uV, int max_uV)
{
	int i;

	switch (ldo_id) {
	case MT6360_LDO3:
		for (i = 0; i < ARRAY_SIZE(MT6360_LDO3_VOSEL_TABLE); i++) {
			int uV = MT6360_LDO3_VOSEL_TABLE[i];
			int step;
			if (!uV)
				continue;
			/* TODO(pihsun): Constants for the step */
			if (uV + 100000 < min_uV)
				continue;
			uV = DIV_ROUND_UP(uV, 10000) * 10000;
			if (uV > max_uV)
				continue;
			step = (uV - MT6360_LDO3_VOSEL_TABLE[i]) / 10000;

			return mt6360_ldo_update_bits(
				MT6360_REG_LDO3_CTRL3,
				MT6360_MASK_LDO3_VOSEL | MT6360_MASK_LDO3_VOCAL,
				(i << MT6360_MASK_LDO3_VOSEL_SHIFT) | step);
		}
		CPRINTS("LDO3 voltage %d - %d out of range", min_uV, max_uV);
		return EC_ERROR_INVAL;
	case MT6360_LDO5:
		for (i = 0; i < ARRAY_SIZE(MT6360_LDO5_VOSEL_TABLE); i++) {
			int uV = MT6360_LDO5_VOSEL_TABLE[i];
			int step;
			if (!uV)
				continue;
			if (uV + 100000 < min_uV)
				continue;
			uV = DIV_ROUND_UP(uV, 10000) * 10000;
			if (uV > max_uV)
				continue;
			step = (uV - MT6360_LDO5_VOSEL_TABLE[i]) / 10000;

			return mt6360_ldo_update_bits(
				MT6360_REG_LDO5_CTRL3,
				MT6360_MASK_LDO5_VOSEL | MT6360_MASK_LDO5_VOCAL,
				(i << MT6360_MASK_LDO5_VOSEL_SHIFT) | step);
		}
		CPRINTS("LDO5 voltage %d - %d out of range", min_uV, max_uV);
		return EC_ERROR_INVAL;
	default:
		return EC_ERROR_INVAL;
	}
}

int mt6360_ldo_get_voltage(enum mt6360_ldo_id ldo_id, int *voltage_uV)
{
	int data;
	int rv;

	/*
	 * TODO(pihsun): Remove duplicate code for LDO3 & LDO5
	 */
	switch (ldo_id) {
	case MT6360_LDO3:
		rv = mt6360_ldo_read8(MT6360_REG_LDO3_CTRL3, &data);
		if (rv) {
			CPRINTS("Error reading LDO3 ctrl3: %d", rv);
			return rv;
		}
		*voltage_uV =
			MT6360_LDO3_VOSEL_TABLE[(data &
						 MT6360_MASK_LDO3_VOSEL) >>
						MT6360_MASK_LDO3_VOSEL_SHIFT];
		if (*voltage_uV == 0) {
			CPRINTS("Unknown LDO3 voltage value: %d", data);
			return EC_ERROR_INVAL;
		}
		*voltage_uV += MIN(10, data & MT6360_MASK_LDO3_VOCAL) * 10000;
		return EC_SUCCESS;
	case MT6360_LDO5:
		rv = mt6360_ldo_read8(MT6360_REG_LDO5_CTRL3, &data);
		if (rv) {
			CPRINTS("Error reading LDO5 ctrl3: %d", rv);
			return rv;
		}
		*voltage_uV =
			MT6360_LDO5_VOSEL_TABLE[(data &
						 MT6360_MASK_LDO5_VOSEL) >>
						MT6360_MASK_LDO5_VOSEL_SHIFT];
		if (*voltage_uV == 0) {
			CPRINTS("Unknown LDO5 voltage value: %d", data);
			return EC_ERROR_INVAL;
		}
		*voltage_uV += MIN(10, data & MT6360_MASK_LDO5_VOCAL) * 10000;
		return EC_SUCCESS;
	default:
		return EC_ERROR_INVAL;
	}
}

/* RGB LED */
int mt6360_led_enable(enum mt6360_led_id led_id, int enable)
{
	if (!IN_RANGE(led_id, 0, MT6360_LED_COUNT))
		return EC_ERROR_INVAL;

	if (enable)
		return mt6360_set_bit(MT6360_REG_RGB_EN,
				      MT6360_MASK_ISINK_EN(led_id));
	return mt6360_clr_bit(MT6360_REG_RGB_EN, MT6360_MASK_ISINK_EN(led_id));
}

int mt6360_led_set_brightness(enum mt6360_led_id led_id, int brightness)
{
	int val;

	if (!IN_RANGE(led_id, 0, MT6360_LED_COUNT))
		return EC_ERROR_INVAL;
	if (!IN_RANGE(brightness, 0, 16))
		return EC_ERROR_INVAL;

	RETURN_ERROR(mt6360_read8(MT6360_REG_RGB_ISINK(led_id), &val));
	val &= ~MT6360_MASK_CUR_SEL;
	val |= brightness;

	return mt6360_write8(MT6360_REG_RGB_ISINK(led_id), val);
}

const struct bc12_drv mt6360_drv = {
	.usb_charger_task = mt6360_usb_charger_task,
	.ramp_allowed = mt6360_ramp_allowed,
	.ramp_max = mt6360_ramp_max,
};

#ifdef CONFIG_BC12_SINGLE_DRIVER
/* provide a default bc12_ports[] for backward compatibility */
struct bc12_config bc12_ports[CHARGE_PORT_COUNT] = {
	[0 ... (CHARGE_PORT_COUNT - 1)] = {
		.drv = &mt6360_drv,
	},
};
#endif /* CONFIG_BC12_SINGLE_DRIVER */
