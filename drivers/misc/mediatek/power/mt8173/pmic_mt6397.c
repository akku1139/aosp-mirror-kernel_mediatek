#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/regmap.h>
#include <linux/irqdomain.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/mfd/mt6397/registers.h>

#include <mt_pmic_wrap.h>
#include <mt-plat/upmu_common.h>

static DEFINE_MUTEX(pmic_lock_mutex);
static DEFINE_MUTEX(pmic_access_mutex);
static DEFINE_SPINLOCK(pmic_smp_spinlock);

/* ============================================================================== */
/* PMIC lock/unlock APIs */
/* ============================================================================== */
void pmic_lock(void)
{
	mutex_lock(&pmic_lock_mutex);
}

void pmic_unlock(void)
{
	mutex_unlock(&pmic_lock_mutex);
}

void pmic_smp_lock(void)
{
	spin_lock(&pmic_smp_spinlock);
}

void pmic_smp_unlock(void)
{
	spin_unlock(&pmic_smp_spinlock);
}

unsigned int pmic_read_interface(unsigned int RegNum, unsigned int *val, unsigned int MASK, unsigned int SHIFT)
{
	unsigned int return_value = 0;
	unsigned int pmic_reg = 0;
	unsigned int rdata;

	mutex_lock(&pmic_access_mutex);

	return_value = pwrap_wacs2(0, (RegNum), 0, &rdata);
	pmic_reg = rdata;
	if (return_value != 0) {
		pr_err("[Power/PMIC]" "[pmic_read_interface] Reg[%x]= pmic_wrap read data fail\n", RegNum);
		mutex_unlock(&pmic_access_mutex);
		return return_value;
	}

	pmic_reg &= (MASK << SHIFT);
	*val = (pmic_reg >> SHIFT);

	mutex_unlock(&pmic_access_mutex);

	return return_value;
}

unsigned int pmic_config_interface(unsigned int RegNum, unsigned int val, unsigned int MASK, unsigned int SHIFT)
{
	unsigned int return_value = 0;
	unsigned int pmic_reg = 0;
	unsigned int rdata;

	mutex_lock(&pmic_access_mutex);

	return_value = pwrap_wacs2(0, (RegNum), 0, &rdata);
	pmic_reg = rdata;
	if (return_value != 0) {
		pr_err("[Power/PMIC]" "[pmic_config_interface] Reg[%x]= pmic_wrap read data fail\n", RegNum);
		mutex_unlock(&pmic_access_mutex);
		return return_value;
	}

	pmic_reg &= ~(MASK << SHIFT);
	pmic_reg |= (val << SHIFT);

	return_value = pwrap_wacs2(1, (RegNum), pmic_reg, &rdata);
	if (return_value != 0) {
		pr_err("[Power/PMIC]" "[pmic_config_interface] Reg[%x]= pmic_wrap read data fail\n", RegNum);
		mutex_unlock(&pmic_access_mutex);
		return return_value;
	}

	mutex_unlock(&pmic_access_mutex);

	return return_value;
}

unsigned int upmu_get_reg_value(unsigned int reg)
{
	unsigned int ret = 0;
	unsigned int reg_val = 0;

	ret = pmic_read_interface(reg, &reg_val, 0xFFFF, 0x0);

	return reg_val;
}

void upmu_set_reg_value(unsigned int reg, unsigned int reg_val)
{
	unsigned int ret = 0;

	ret = pmic_config_interface(reg, reg_val, 0xFFFF, 0x0);
}

static int pmic_mt6397_probe(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id mt6397_pmic_of_match[] = {
	{ .compatible = "mediatek,mt6397-pmic", },
	{ }
};

static struct platform_driver pmic_mt6397_driver = {
	.driver = {
		.name = "mt6397-pmic",
		.owner = THIS_MODULE,
		.of_match_table = mt6397_pmic_of_match,
	},
	.probe	= pmic_mt6397_probe,
};

module_platform_driver(pmic_mt6397_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Flora Fu <flora.fu@mediatek.com>");
MODULE_DESCRIPTION("PMIC Common Driver for MediaTek MT6397 PMIC");
MODULE_ALIAS("platform:mt6397-pmic");
