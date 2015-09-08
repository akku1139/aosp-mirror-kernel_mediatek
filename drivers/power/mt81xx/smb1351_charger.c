#define pr_fmt(fmt) "SMB1351 %s: " fmt, __func__
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/reboot.h>
#include <linux/switch.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>
#include <linux/wakelock.h>
#include "mt_charging.h"
#include "mt_battery_common.h"
#include <mt-plat/upmu_common.h>
#include <mt-plat/mt_reboot.h>
#include <mt-plat/mt_boot.h>

/**********************************************************
 *
 *    [Define]
 *
 **********************************************************/

#define smb1351_access_err(rc, write, reg) \
	do { \
		if (rc) {\
			pr_err("%s reg: %02xh failed\n", (write) ? "write" : "read", reg); \
		}\
	} while (0)

#define STATUS_OK	0
#define STATUS_UNSUPPORTED	-1
#define smb1351_read_access 0
#define smb1351_write_access 1
#define GETARRAYNUM(array) (sizeof(array)/sizeof(array[0]))

/**********************************************************
  *
  *   [Global Variable]
  *
  *********************************************************/

static u8 g_reg_value_smb1351;
static bool charger_status = false;

static const u32 VCDT_HV_VTH[] = {
	BATTERY_VOLT_04_200000_V, BATTERY_VOLT_04_250000_V, BATTERY_VOLT_04_300000_V,
	BATTERY_VOLT_04_350000_V,
	BATTERY_VOLT_04_400000_V, BATTERY_VOLT_04_450000_V, BATTERY_VOLT_04_500000_V,
	BATTERY_VOLT_04_550000_V,
	BATTERY_VOLT_04_600000_V, BATTERY_VOLT_06_000000_V, BATTERY_VOLT_06_500000_V,
	BATTERY_VOLT_07_000000_V,
	BATTERY_VOLT_07_500000_V, BATTERY_VOLT_08_500000_V, BATTERY_VOLT_09_500000_V,
	BATTERY_VOLT_10_500000_V
};

static int fast_chg_current[] = {
	100000, 120000, 140000, 160000, 180000, 200000, 220000, 240000, 260000, 280000, 300000, 340000, 360000,
		380000, 400000, 450000
};

static int input_current[] = {
	50000, 68500, 100000, 110000, 120000, 130000, 150000, 160000, 170000, 180000, 200000, 220000, 250000, 300000
};

enum temp_state {
	unknown_temp_state = 0,
	less_than_15,
	from_15_to_100,
	from_100_to_500,
	from_500_to_600,
	greater_than_600,
};

extern int hw_charger_type_detection(void);
static int of_get_smb1351_platform_data(struct device *dev);

struct smb1351_charger {
	struct i2c_client	*client;
	struct device		*dev;
	struct mutex		read_write_lock;
	struct wake_lock	jeita_setting_wake_lock;
	int	last_charger_type;
	int	last_temp_state;
	int	otg_en_gpio;
};
struct smb1351_charger *chip;

/**********************************************************
  *
  *   [I2C Function For Read/Write smb1351]
  *
  *********************************************************/
static int __smb1351_read_reg(u8 reg, u8 *val)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(chip->client, reg);
	if (ret < 0) {
		pr_err("i2c read fail: can't read from %02x: %d\n", reg, ret);
		return ret;
	} else {
		*val = ret;
	}

	return 0;
}

static int __smb1351_write_reg(int reg, u8 val)
{
	s32 ret;

	ret = i2c_smbus_write_byte_data(chip->client, reg, val);
	if (ret < 0) {
		pr_err("i2c write fail: can't write %02x to %02x: %d\n",
			val, reg, ret);
		return ret;
	}
	return 0;
}

static int smb1351_read_reg(int reg, u8 *val)
{
	int rc;

	mutex_lock(&chip->read_write_lock);
	rc = __smb1351_read_reg(reg, val);
	mutex_unlock(&chip->read_write_lock);

	return rc;
}

static int smb1351_masked_write(int reg, u8 mask, u8 val)
{
	s32 rc;
	u8 temp;
	int i;

	for (i = 0; i < 10; i++) {
		mutex_lock(&chip->read_write_lock);
		rc = __smb1351_read_reg(reg, &temp);
		mutex_unlock(&chip->read_write_lock);
		if (!rc || i == 9)
			break;
		else
			msleep(100);
	}
	if (rc) {
		pr_err("smb1351_read_reg Failed: reg=%03X, rc=%d\n", reg, rc);
		goto out;
	}
	temp &= ~mask;
	temp |= val & mask;
	for (i = 0; i < 10; i++) {
		mutex_lock(&chip->read_write_lock);
		rc = __smb1351_write_reg(reg, temp);
		mutex_unlock(&chip->read_write_lock);
		if (!rc || i == 9)
			break;
		else
			msleep(100);
	}
	if (rc) {
		pr_err("smb1351_write Failed: reg=%03X, rc=%d\n", reg, rc);
	}
out:
	return rc;
}

static int smb1351_enable_volatile_access(void)
{
	int rc;
	// BQ configuration volatile access, 30h[6] = 1
	rc = smb1351_masked_write(0x30, 0x40, 0x40);
	smb1351_access_err(rc, smb1351_write_access, 0x30);
	return 0;
}

static void smb1351_dump_register(void)
{
	int i = 0;
	char tmp_buf[64], *buf;
	u8 reg;
	buf = (char *)kmalloc(sizeof(tmp_buf) * sizeof(char) * 30, GFP_KERNEL);

	smb1351_enable_volatile_access();
	sprintf(tmp_buf, "smb1351 Configuration Registers Detail\n"
						"==================\n");
	strcpy(buf, tmp_buf);

	for (i = 0; i <= 20; i++) {
		smb1351_read_reg(0x0+i, &reg);
		sprintf(tmp_buf, "Reg%02xh:\t0x%02x\n", 0x0+i, reg);
		strcat(buf, tmp_buf);
	}
	pr_info("%s", buf);
	sprintf(tmp_buf, "smb1351 Configuration Registers Detail\n"
						"==================\n");
	strcpy(buf, tmp_buf);
	for (i = 0; i <= 26; i++) {
		smb1351_read_reg(0x15+i, &reg);
		sprintf(tmp_buf, "Reg%02xh:\t0x%02x\n", 0x15+i, reg);
		strcat(buf, tmp_buf);
	}
	pr_info("%s", buf);
	sprintf(tmp_buf, "smb1351 Configuration Registers Detail\n"
						"==================\n");
	strcpy(buf, tmp_buf);
	for (i = 0; i <= 24; i++) {
		smb1351_read_reg(0x30+i, &reg);
		sprintf(tmp_buf, "Reg%02xh:\t0x%02x\n", 0x30+i, reg);
		strcat(buf, tmp_buf);
	}
	pr_info("%s", buf);
	kfree(buf);
}

static void smb1351_enable_AICL(bool enable)
{
	int rc;
	smb1351_enable_volatile_access();
	if (enable) {
		// 02h[4] = "1"
		rc = smb1351_masked_write(0x2, 0x10, 0x10);
		smb1351_access_err(rc, smb1351_write_access, 0x2);
	} else {
		// 02h[4] = "0"
		rc = smb1351_masked_write(0x2, 0x10, 0x0);
		smb1351_access_err(rc, smb1351_write_access, 0x2);
	}
}

static void smb1351_enable_charging(bool enable)
{
	int rc;
	smb1351_enable_volatile_access();
	if (enable) {
		// Charging Enable, 06h[6:5] = "11"
		rc = smb1351_masked_write(0x6, 0x60, 0x60);
		smb1351_access_err(rc, smb1351_write_access, 0x6);
	} else {
		// Charging Disable, 06h[6:5] = "10"
		rc = smb1351_masked_write(0x6, 0x60, 0x40);
		smb1351_access_err(rc, smb1351_write_access, 0x6);
	}
}

static void smb1351_AC_1A_setting(void)
{
	int rc;
	// BQ configuration volatile access, 30h[6] = 1
	rc = smb1351_enable_volatile_access();
	smb1351_access_err(rc, smb1351_write_access, 0x30);
	// Set Input current = command register, 31h[3] = "1"
	rc = smb1351_masked_write(0x31, 0x8, 0x8);
	smb1351_access_err(rc, smb1351_write_access, 0x31);
	// Set USB AC control = USB AC, 31h[0]  ="1"
	rc = smb1351_masked_write(0x31, 0x1, 0x1);
	smb1351_access_err(rc, smb1351_write_access, 0x31);
	// Disable AICL, 02h[4] = "0"
	smb1351_enable_AICL(false);
	// Set IUSB_IN = 1000 mA, 00h[3:0] = "0010"
	rc = smb1351_masked_write(0x0, 0xf, 0x2);
	smb1351_access_err(rc, smb1351_write_access, 0x0);
	// Enable AICL, 2h[4] = "1"
	smb1351_enable_AICL(true);
}

static void smb1351_initial_setting(void)
{
	int rc;
	// BQ configuration volatile access, 30h[6] = 1
	smb1351_enable_volatile_access();
	// Set AICL fail forces suspend mode, 08h[7] = "0"
	rc = smb1351_masked_write(0x8, 0x80, 0x0);
	smb1351_access_err(rc, smb1351_write_access, 0x8);
	// Force QC2.0 mode, 34h[5] = "1"
	rc = smb1351_masked_write(0x34, 0x20, 0x20);
	smb1351_access_err(rc, smb1351_write_access, 0x34);
	// Set pre-charge current = 200mA, 01h[7:5] = "000"
	rc = smb1351_masked_write(0x01, 0xE0, 0x0);
	smb1351_access_err(rc, smb1351_write_access, 0x01);
	// Set fast charge current = 2200mA, 00h[7:4] = "0110"
	rc = smb1351_masked_write(0x0, 0xF0, 0x60);
	smb1351_access_err(rc, smb1351_write_access, 0x0);
	// Set soft cold current compensation = 1000mA, 0Eh[5] = "1"
	rc = smb1351_masked_write(0xE, 0x20, 0x20);
	smb1351_access_err(rc, smb1351_write_access, 0xE);
	// Disable HW JEITA, 07h[4] = "1"
	rc = smb1351_masked_write(0x7, 0x10, 0x10);
	smb1351_access_err(rc, smb1351_write_access, 0x7);
	// Set charger disable, 06h[6:5] = "10"
	smb1351_enable_charging(false);
	// Set charger voltage = 4.4V, 03h[5:0] = "101101"
	rc = smb1351_masked_write(0x3, 0x3F, 0x2D);
	smb1351_access_err(rc, smb1351_write_access, 0x3);
	// Set charger enable, 06h[6:5] = "11"
	smb1351_enable_charging(true);
}

/**********************************************************
  *
  *   [Internal Function]
  *
  *********************************************************/

u32 charging_value_to_parameter(const u32 *parameter, const u32 array_size, const u32 val)
{
	if (val < array_size)
		return parameter[val];

	battery_log(BAT_LOG_CRTI, "Can't find the parameter \r\n");
	return parameter[0];
}

u32 charging_parameter_to_value(const u32 *parameter, const u32 array_size, const u32 val)
{
	u32 i;

	for (i = 0; i < array_size; i++) {
		if (val == *(parameter + i))
			return i;
	}

	battery_log(BAT_LOG_CRTI, "NO register value match. val=%d\r\n", val);
	/* TODO: ASSERT(0);      // not find the value */
	return 0;
}

static int get_closest_fast_chg_current(int target_current)
{
	int i;
	for (i = ARRAY_SIZE(fast_chg_current) - 1; i >= 0; i--) {
		if (fast_chg_current[i] <= target_current)
			break;
	}

	if (i < 0) {
		pr_err("Invalid fast_chg current setting %d mA\n",
						target_current/100);
		i = 0;
	}
	return i;
}

static int get_closest_input_current(int target_current)
{
	int i;
	for (i = ARRAY_SIZE(input_current) - 1; i >= 0; i--) {
		if (input_current[i] <= target_current)
			break;
	}

	if (i < 0) {
		pr_err("Invalid input current setting %dmA\n",
						target_current/100);
		i = 0;
	}
	return i;
}

static int smb1351_check_AICL(unsigned int target_current)
{
	int i, rc;
	u8 reg;
	i = get_closest_input_current(target_current * 100);

	rc = smb1351_read_reg(0x36, &reg);
	smb1351_access_err(rc, smb1351_read_access, 0x36);

	if (reg & 0x80) {
		reg &= 0xf;
		 if (reg <= i)
			return 0;
	}
	return 1;
}

static void smb1351_recharge_func(void)
{
	int rc;
	u8 reg = 0;
	rc = smb1351_read_reg(0x3A, &reg);
	smb1351_access_err(rc, smb1351_read_access, 0x3A);
	reg &= 0x20;
	if (BMT_status.SOC <= 98 && reg) {
		smb1351_enable_charging(false);
		smb1351_enable_charging(true);
	}
}

static void smb1351_JEITA_config(int Vchg, bool enable,
					int fast_chg_target_current)
{
	int rc, i;
	smb1351_enable_volatile_access();
	if (Vchg) {
		// Vchg=4.4V , 03h[5:0]="101101"
		rc = smb1351_masked_write(0x3, 0x3F, 0x2D);
		smb1351_access_err(rc, smb1351_write_access, 0x3);
	} else {
		// Vchg=4.1V , 03h[5:0]="011110"
		rc = smb1351_masked_write(0x3, 0x3F, 0x1E);
		smb1351_access_err(rc, smb1351_write_access, 0x3);
	}

	smb1351_enable_charging(enable);

	// Set Fast Charge Current = fast_chg_target_current mA
	i = get_closest_fast_chg_current(fast_chg_target_current * 100);
	rc = smb1351_masked_write(0x0, 0xf0, i << 4);
	smb1351_access_err(rc, smb1351_write_access, 0x0);
}

static void smb1351_JEITA_Rule(void){
	int temp, rc;
	int Vchg;
	u8 reg = 0;

	wake_lock(&chip->jeita_setting_wake_lock);
	smb1351_enable_volatile_access();
	// Set Hard Hot Limit = 72 Deg. C, 0Bh[5:4] = "11"
	rc = smb1351_masked_write(0xB, 0x30, 0x30);
	smb1351_access_err(rc, smb1351_write_access, 0xB);
	// Set Soft Hot Limit Behavior = No Response,  07h[3:2] = "00"
	rc = smb1351_masked_write(0x7, 0xC, 0x0);
	smb1351_access_err(rc, smb1351_write_access, 0x7);
	// Set Soft Cold temp limit = No Response,  07h[1:0] = "00"
	rc = smb1351_masked_write(0x7, 0x3, 0x0);
	smb1351_access_err(rc, smb1351_write_access, 0x7);

	temp = BMT_status.temperature * 10;
	if (chip->last_temp_state == less_than_15 &&
		temp <= 45)
		temp = 10;
	else if (chip->last_temp_state == from_15_to_100 &&
		temp <= 130)
		temp = 90;
	else if (chip->last_temp_state == from_500_to_600 &&
		temp >= 470)
		temp = 550;
	else if (chip->last_temp_state == greater_than_600 &&
		temp >= 570)
		temp = 650;

	rc = smb1351_read_reg(0x3, &reg);
	smb1351_access_err(rc, smb1351_write_access, 0x3);
	reg &= 0x3F;
	if (reg == 0x2D)
		Vchg = 1;
	else
		Vchg = 0;

	if (temp < 15) {
		smb1351_JEITA_config(1, false, 1400);
		chip->last_temp_state = less_than_15;
	}
	else if (temp >= 15 && temp < 100) {
		smb1351_JEITA_config(1, true, 1000);
		chip->last_temp_state = from_15_to_100;
		smb1351_recharge_func();
	}
	else if (temp >= 100 && temp < 500) {
		smb1351_JEITA_config(1, true, 2200);
		chip->last_temp_state = from_100_to_500;
		smb1351_recharge_func();
	}
	else if (temp >= 500 && temp < 600) {
		if (Vchg && BMT_status.bat_vol >= 4100)
			smb1351_JEITA_config(1, false, 1400);
		else
			smb1351_JEITA_config(0, true, 2200);
		chip->last_temp_state = from_500_to_600;
	}
	else if (temp >= 600) {
		smb1351_JEITA_config(1, false, 1400);
		chip->last_temp_state = greater_than_600;
	}
	else {
		pr_err("unknown temperature range\n");
		chip->last_temp_state = unknown_temp_state;
	}
	wake_unlock(&chip->jeita_setting_wake_lock);
}

static u32 bmt_find_closest_level(const u32 *pList, u32 number, u32 level)
{
	u32 i;
	u32 max_value_in_last_element;

	if (pList[0] < pList[1])
		max_value_in_last_element = true;
	else
		max_value_in_last_element = false;

	if (max_value_in_last_element == true) {
		for (i = (number - 1); i != 0; i--) {	/* max value in the last element */
			if (pList[i] <= level)
				return pList[i];
		}

		battery_log(BAT_LOG_CRTI, "Can't find closest level, small value first \r\n");
		return pList[0];
	}

	for (i = 0; i < number; i++) {	/* max value in the first element */
		if (pList[i] <= level)
			return pList[i];
	}

	battery_log(BAT_LOG_CRTI, "Can't find closest level, large value first \r\n");
	return pList[number - 1];

}

static u32 charging_hw_init(void *data)
{
	u32 status = STATUS_OK;

	upmu_set_rg_bc11_bb_ctrl(1);	/* BC11_BB_CTRL */
	upmu_set_rg_bc11_rst(1);	/* BC11_RST */
	return status;
}

static u32 charging_dump_register(void *data)
{
	u32 status = STATUS_OK;

	smb1351_dump_register();

	return status;
}

static u32 charging_enable(void *data)
{
	u32 status = STATUS_OK;
	u32 enable = *(u32 *) (data);

	smb1351_enable_charging((!enable) ? false : true);

	return status;
}

static u32 charging_set_cv_voltage(void *data)
{
	u32 status = STATUS_OK;
	int rc;

	smb1351_enable_volatile_access();
	// set cv to 4.4v, 03h[5:0] = "101101"
	rc = smb1351_masked_write(0x3, 0x3F, 0x2D);
	smb1351_access_err(rc, smb1351_write_access, 0x3);

	return status;
}

static u32 charging_get_current(void *data)
{
	u32 status = STATUS_OK;
	int rc;
	u8 reg;

	smb1351_enable_volatile_access();
	rc = smb1351_read_reg(0x39, &reg);
	smb1351_access_err(rc, smb1351_read_access, 0x39);

	*(u32 *) data = fast_chg_current[reg & 0xf];


	return status;
}



static u32 charging_set_current(void *data)
{
	u32 status = STATUS_OK;
	int current_value = *(u32 *) data;
	int i, rc;

	pr_info("Setting current limit: %d\n", current_value);
	smb1351_enable_volatile_access();
	i = get_closest_fast_chg_current(current_value);
	rc = smb1351_masked_write(0x0, 0xf0, i << 4);
	smb1351_access_err(rc, smb1351_write_access, 0x0);


	return status;
}


static u32 charging_set_input_current(void *data)
{
	u32 status = STATUS_OK;
	int current_value = *(u32 *) data;
	int i, rc;

	pr_info("Setting input current limit: %d\n", current_value);
	smb1351_enable_volatile_access();
	i = get_closest_input_current(current_value);
	smb1351_enable_AICL(false);
	rc = smb1351_masked_write(0x0, 0xf, i);
	smb1351_access_err(rc, smb1351_write_access, 0x0);
	smb1351_enable_AICL(true);

	return status;
}

static u32 charging_get_input_current(void *data)
{
	int rc;
	u8 reg;

	smb1351_enable_volatile_access();
	rc = smb1351_read_reg(0x36, &reg);
	smb1351_access_err(rc, smb1351_read_access, 0x36);

	*(u32 *) data = input_current[reg & 0xf];
	return STATUS_OK;
}

static u32 charging_get_charging_status(void *data)
{
	u32 status = STATUS_OK;
	int rc;
	u8 reg;
	smb1351_enable_volatile_access();
	rc = smb1351_read_reg(0x42, &reg);
	smb1351_access_err(rc, smb1351_read_access, 0x42);
	*(u32 *) data = (reg & 0x1);

	return status;
}

static u32 charging_reset_watch_dog_timer(void *data)
{
	u32 status = STATUS_OK;

	return status;
}

static u32 charging_set_hv_threshold(void *data)
{
	u32 status = STATUS_OK;

	u32 set_hv_voltage;
	u32 array_size;
	u16 register_value;
	u32 voltage = *(u32 *) (data);

	array_size = GETARRAYNUM(VCDT_HV_VTH);
	set_hv_voltage = bmt_find_closest_level(VCDT_HV_VTH, array_size, voltage);
	register_value = charging_parameter_to_value(VCDT_HV_VTH, array_size, set_hv_voltage);
	upmu_set_rg_vcdt_hv_vth(register_value);

	return status;
}

static u32 charging_get_hv_status(void *data)
{
	u32 status = STATUS_OK;

	*(bool *) (data) = upmu_get_rgs_vcdt_hv_det();
	return status;
}


static u32 charging_get_battery_status(void *data)
{
	u32 status = STATUS_OK;

	/* upmu_set_baton_tdet_en(1); */
	upmu_set_rg_baton_en(1);
	*(bool *) (data) = upmu_get_rgs_baton_undet();

	return status;
}


static u32 charging_get_charger_det_status(void *data)
{
	u32 status = STATUS_OK;

	*(bool *) (data) = upmu_get_rgs_chrdet();

	return status;
}

static u32 charging_get_charger_type(void *data)
{
	u32 status = STATUS_OK;
	*(int *) (data) = hw_charger_type_detection();
	return status;
}

static u32 charging_get_is_pcm_timer_trigger(void *data)
{
	u32 status = STATUS_OK;
/*  TODO: depend on spm, which would be porting later.
	if (slp_get_wake_reason() == WR_PCM_TIMER)
		*(bool *) (data) = true;
	else
		*(bool *) (data) = false;

	battery_log(BAT_LOG_CRTI, "slp_get_wake_reason=%d\n", slp_get_wake_reason());
*/
	*(bool *) (data) = false;
	return status;
}

static u32 charging_set_platform_reset(void *data)
{
	u32 status = STATUS_OK;

	battery_log(BAT_LOG_CRTI, "charging_set_platform_reset\n");

#if 0				/* need porting of orderly_reboot(). */
	if (system_state == SYSTEM_BOOTING)
		arch_reset(0, NULL);
	else
		orderly_reboot(true);
#endif
	arch_reset(0, NULL);

	return status;
}

static u32 charging_get_platform_boot_mode(void *data)
{
	u32 status = STATUS_OK;

	*(u32 *) (data) = get_boot_mode();

	battery_log(BAT_LOG_CRTI, "get_boot_mode=%d\n", get_boot_mode());

	return status;
}

static u32 charging_enable_powerpath(void *data)
{
	u32 status = STATUS_OK;
	u32 enable = *(u32 *) (data);

	smb1351_enable_volatile_access();
	if (enable == true)
		smb1351_masked_write(0x6, 0x60, 0x60);
	else
		smb1351_masked_write(0x6, 0x60, 0x40);

	return status;
}

static u32 charging_boost_enable(void *data)
{
	u32 status = STATUS_OK;
	u32 enable = *(u32 *) (data);
	int rc;

	smb1351_enable_volatile_access();
	if (enable == true) {
		pr_info("Enable OTG\n");
		// OTG current limit = 500mA, 0Ah[3:2] = "01"
		rc = smb1351_masked_write(0xA, 0xC, 0x4);
		smb1351_access_err(rc, smb1351_write_access, 0xA);
		gpio_direction_output(chip->otg_en_gpio, 1);
		// OTG current limit = 1000 mA, 0Ah[3:2] = "11"
		rc = smb1351_masked_write(0xA, 0xC, 0xC);
		smb1351_access_err(rc, smb1351_write_access, 0xA);
	} else {
		pr_info("Disable OTG\n");
		// OTG current limit = 500mA, 0Ah[3:2] = "01"
		rc = smb1351_masked_write(0xA, 0xC, 0x4);
		smb1351_access_err(rc, smb1351_write_access, 0xA);
		gpio_direction_output(chip->otg_en_gpio, 0);
	}

	return status;
}

static u32(*const charging_func[CHARGING_CMD_NUMBER]) (void *data) = {
charging_hw_init,
	    charging_dump_register,
	    charging_enable,
	    charging_set_cv_voltage,
	    charging_get_current,
	    charging_set_current,
	    charging_get_input_current,
	    charging_set_input_current,
	    charging_get_charging_status,
	    charging_reset_watch_dog_timer,
	    charging_set_hv_threshold,
	    charging_get_hv_status,
	    charging_get_battery_status,
	    charging_get_charger_det_status,
	    charging_get_charger_type,
	    charging_get_is_pcm_timer_trigger,
	    charging_set_platform_reset,
	    charging_get_platform_boot_mode, charging_enable_powerpath, charging_boost_enable};

s32 smb1351_control_interface(int cmd, void *data)
{
	s32 status;

	if (cmd < CHARGING_CMD_NUMBER)
		status = charging_func[cmd] (data);
	else
		return STATUS_UNSUPPORTED;

	return status;
}

/*
 *	return true if cv is set to lower level
 *	since battery is in high temp.
 */
bool smb1351_skip_battery_100Percent_tracking(void)
{
	int rc;
	u8 reg = 0;
	rc = smb1351_read_reg(0x3, &reg);
	smb1351_access_err(rc, smb1351_write_access, 0x3);
	reg &= 0x3F;
	return (reg == 0x1E);
}
EXPORT_SYMBOL(smb1351_skip_battery_100Percent_tracking);

void smb1351_charging_algorithm(void)
{
	int i = 0;
	if (chip->last_charger_type != BMT_status.charger_type) {
		pr_info("Setting Charging Current for cable type : %d -> %d\n",
			chip->last_charger_type, BMT_status.charger_type);
		smb1351_initial_setting();
		if (BMT_status.charger_exist) {
			do {
				if (BMT_status.charger_type != STANDARD_HOST &&
					BMT_status.charger_type != CHARGING_HOST) {
					smb1351_AC_1A_setting();
				}
				if (i > 10)
					break;
				else
					i++;
			} while (!smb1351_check_AICL(500));
		}
		pr_info("Set Charging Current Done for cable type : %d\n", BMT_status.charger_type);
		chip->last_charger_type = BMT_status.charger_type;
	}
	if (BMT_status.charger_exist)
		smb1351_JEITA_Rule();
}
EXPORT_SYMBOL(smb1351_charging_algorithm);

static ssize_t reg_status_get(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	int i = 0;
	char tmp_buf[64];
	u8 reg;

	smb1351_enable_volatile_access();
	sprintf(tmp_buf, "smb1351 Configuration Registers Detail\n"
						"==================\n");
	strcpy(buf, tmp_buf);

	for (i = 0; i <= 20; i++) {
		smb1351_read_reg(0x0+i, &reg);
		sprintf(tmp_buf, "Reg%02xh:\t0x%02x\n", 0x0+i, reg);
		strcat(buf, tmp_buf);
	}
	for (i = 0; i <= 26; i++) {
		smb1351_read_reg(0x15+i, &reg);
		sprintf(tmp_buf, "Reg%02xh:\t0x%02x\n", 0x15+i, reg);
		strcat(buf, tmp_buf);
	}
	for (i = 0; i <= 24; i++) {
		smb1351_read_reg(0x30+i, &reg);
		sprintf(tmp_buf, "Reg%02xh:\t0x%02x\n", 0x30+i, reg);
		strcat(buf, tmp_buf);
	}

	return strlen(buf);
}

static ssize_t chargeric_status_get(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	return sprintf(buf, "%d\n", charger_status);
}

static DEVICE_ATTR(chargerIC_status, S_IRUGO , chargeric_status_get, NULL);
static DEVICE_ATTR(reg_status, S_IRUGO, reg_status_get, NULL);

static struct attribute *smb1351_charger_attributes[] = {
	&dev_attr_chargerIC_status.attr,
	&dev_attr_reg_status.attr,
	NULL
};

static const struct attribute_group smb1351_charger_group = {
	.attrs = smb1351_charger_attributes,
};

static int smb1351_driver_suspend(struct i2c_client *client, pm_message_t mesg)
{
	pr_notice("\n");

	return 0;
}

static int smb1351_driver_resume(struct i2c_client *client)
{
	pr_notice("\n");

	return 0;
}

static void smb1351_driver_shutdown(struct i2c_client *client)
{
	pr_notice("\n");
}

static ssize_t show_smb1351_access(struct device *dev, struct device_attribute *attr, char *buf)
{
	pr_info("0x%x\n", g_reg_value_smb1351);
	return sprintf(buf, "0x%x\n", g_reg_value_smb1351);
}

static ssize_t store_smb1351_access(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t size)
{
	int ret = 0;
	char *pvalue = NULL;
	unsigned int reg_value = 0;
	unsigned int reg_address = 0;

	pr_info("\n");
	smb1351_enable_volatile_access();
	if (buf != NULL && size != 0) {
		pr_info("buf is %s and size is %d\n", buf, (int)size);
		reg_address = simple_strtoul(buf, &pvalue, 16);

		if (size > 3) {
			reg_value = simple_strtoul((pvalue + 1), NULL, 16);
			pr_info("write smb1351 reg 0x%x with value 0x%x !\n",
				reg_address, reg_value);
			ret = smb1351_masked_write(reg_address, 0xFF, reg_value);
		} else {
			ret = smb1351_read_reg(reg_address, &g_reg_value_smb1351);
			pr_info("read smb1351 reg 0x%x with value 0x%x !\n",
				reg_address, g_reg_value_smb1351);
			pr_info
			    ("Please use \"cat smb1351_access\" to get value\r\n");
		}
	}
	return size;
}

static DEVICE_ATTR(smb1351_access, S_IWUSR | S_IRUGO, show_smb1351_access, store_smb1351_access);

static int smb1351_user_space_probe(struct platform_device *dev)
{
	int ret_device_file = 0;

	pr_info("smb1351_user_space_probe!\n");
	ret_device_file = device_create_file(&(dev->dev), &dev_attr_smb1351_access);

	return 0;
}

struct platform_device smb1351_user_space_device = {
	.name = "smb1351-user",
	.id = -1,
};

static struct platform_driver smb1351_user_space_driver = {
	.probe = smb1351_user_space_probe,
	.driver = {
		   .name = "smb1351-user",
		   },
};
static int smb1351_driver_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret;

	pr_info("++\n");
	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		pr_err("Couldn't allocate memory\n");
		return -ENOMEM;
	}
	chip->client = client;
	chip->dev = &client->dev;
	chip->last_charger_type = CHARGER_UNKNOWN;
	chip->last_temp_state = unknown_temp_state;

	mutex_init(&chip->read_write_lock);
	wake_lock_init(&chip->jeita_setting_wake_lock,
			WAKE_LOCK_SUSPEND, "jeita_setting_wake_lock");
	i2c_set_clientdata(client, chip);
	ret = of_get_smb1351_platform_data(chip->dev);
	if (ret) {
		pr_err("failed to get smb1351 platform data through dt!!\n");
		return ret;
	}

	ret = gpio_request_one(chip->otg_en_gpio, GPIOF_OUT_INIT_LOW,
			"OTG_EN");
	if (ret) {
		pr_err("Couldn't request GPIO for OTG pinctrl\n");
		return ret;
	}

	bat_charger_register(smb1351_control_interface);

	smb1351_dump_register();
	/* smb1351 user space access interface */
	ret = platform_device_register(&smb1351_user_space_device);
	if (ret) {
		pr_err("Unable to device register(%d)\n", ret);
		return ret;
	}
	ret = platform_driver_register(&smb1351_user_space_driver);
	if (ret) {
		pr_err("Unable to register driver (%d)\n", ret);
		return ret;
	}

	ret = smb1351_enable_volatile_access();
	if (!ret)
		charger_status = true;
	ret = sysfs_create_group(&client->dev.kobj, &smb1351_charger_group);
	if (ret) {
		pr_err("unable to create the sysfs\n");
	}
	pr_info("--\n");
	return 0;
}

static int smb1351_driver_remove(struct i2c_client *client)
{
	mutex_destroy(&chip->read_write_lock);
	wake_lock_destroy(&chip->jeita_setting_wake_lock);
	gpio_free(chip->otg_en_gpio);
	return 0;
}

static const struct i2c_device_id smb1351_charger_id[] = { {"smb1351-charger", 0}, {} };

#ifdef CONFIG_OF
static const struct of_device_id smb1351_match_table[] = {
	{.compatible = "qcom,smb1351-charger"},
	{},
};

MODULE_DEVICE_TABLE(of, smb1351_match_table);

static int of_get_smb1351_platform_data(struct device *dev)
{
	if (dev->of_node) {
                const struct of_device_id *match;

                match = of_match_device(of_match_ptr(smb1351_match_table), dev);
                if (!match) {
                        pr_err("Error: No device match found\n");
                        return -ENODEV;
                }
        }
	chip->otg_en_gpio = of_get_named_gpio(dev->of_node, "otg-gpio", 0);
	pr_info("OTG enable gpio: %d\n", chip->otg_en_gpio);
	return 0;
}
#else
static int of_get_smb1351_platform_data(struct device *dev)
{
	return 0;
}
#endif

static struct i2c_driver smb1351_charger_driver = {
	.driver = {
		   .name = "smb1351-charger",
		   .owner		= THIS_MODULE,
#ifdef CONFIG_OF
		   .of_match_table = smb1351_match_table,
#endif
		   },
	.probe = smb1351_driver_probe,
	.shutdown = smb1351_driver_shutdown,
	.suspend = smb1351_driver_suspend,
	.resume = smb1351_driver_resume,
	.remove		= smb1351_driver_remove,
	.id_table = smb1351_charger_id,
};

module_i2c_driver(smb1351_charger_driver);


MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SMB1351 Charger Driver");

