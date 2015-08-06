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
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/syscore_ops.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/mfd/mt6397/registers.h>

#include <mt-plat/mt_pmic_wrap.h>
#include <mt-plat/upmu_common.h>
#include <pmic_mt6397.h>
#include <../base/power/mt8173/mt_spm_sleep.h>

#if defined(CONFIG_MTK_KERNEL_POWER_OFF_CHARGING)
#include <mt-plat/mt_boot.h>
#include <mt-plat/mt_reboot.h>
#include <mt-plat/mt_boot_common.h>
#endif

static DEFINE_MUTEX(pmic_lock_mutex);
static DEFINE_MUTEX(pmic_access_mutex);
static DEFINE_SPINLOCK(pmic_smp_spinlock);

#if defined(CONFIG_MTK_KERNEL_POWER_OFF_CHARGING)
static bool long_pwrkey_press;
static unsigned long timer_pre;
static unsigned long timer_pos;
#define LONG_PWRKEY_PRESS_TIME		(500*1000000)	/* 500ms */
#endif

static struct mt6397_chip_priv *mt6397_chip;

static struct mt_wake_event mt6397_event = {
	.domain = "PMIC"
};

static struct mt_wake_event_map pwrkey_wev = {
	.domain = "PMIC",
	.code = RG_INT_STATUS_PWRKEY,
	.we = WEV_PWR,
};

static struct mt_wake_event_map rtc_wev = {
	.domain = "PMIC",
	.code = RG_INT_STATUS_RTC,
	.we = WEV_RTC,
};

static struct mt_wake_event_map charger_wev = {
	.domain = "PMIC",
	.code = RG_INT_STATUS_CHRDET,
	.we = WEV_CHARGER,
};

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

/* mt6397 irq chip clear event status for given event mask. */
static void mt6397_ack_events_locked(struct mt6397_chip_priv *chip, unsigned int event_mask)
{
	unsigned int val;

	pwrap_write(chip->int_stat[0], event_mask & 0xFFFF);
	pwrap_write(chip->int_stat[1], (event_mask >> 16) & 0xFFFF);
	pwrap_read(chip->int_stat[0], &val);
	pwrap_read(chip->int_stat[1], &val);
}

/* mt6397 irq chip event read. */
static unsigned int mt6397_get_events(struct mt6397_chip_priv *chip)
{
	/* value does not change in case of pwrap_read() error,
	 * so we must have valid defaults */
	unsigned int events[2] = { 0 };

	pmic_lock();
	pwrap_read(chip->int_stat[0], &events[0]);
	pwrap_read(chip->int_stat[1], &events[1]);
	pmic_unlock();

	return (events[1] << 16) | (events[0] & 0xFFFF);
}

/* mt6397 irq chip event mask read: debugging only */
static unsigned int mt6397_get_event_mask_locked(struct mt6397_chip_priv *chip)
{
	/* value does not change in case of pwrap_read() error,
	 * so we must have valid defaults */
	unsigned int event_mask[2] = { 0 };

	pwrap_read(chip->int_con[0], &event_mask[0]);
	pwrap_read(chip->int_con[1], &event_mask[1]);

	return (event_mask[1] << 16) | (event_mask[0] & 0xFFFF);
}

static unsigned int mt6397_get_event_mask(struct mt6397_chip_priv *chip)
{
	/* value does not change in case of pwrap_read() error,
	 * so we must have valid defaults */
	unsigned int res;

	pmic_lock();
	res = mt6397_get_event_mask_locked(chip);
	pmic_unlock();

	return res;
}

/* mt6397 irq chip event mask write: initial setup */
static void mt6397_set_event_mask_locked(struct mt6397_chip_priv *chip, unsigned int event_mask)
{
	unsigned int val;

	pwrap_write(chip->int_con[0], event_mask & 0xFFFF);
	pwrap_write(chip->int_con[1], (event_mask >> 16) & 0xFFFF);
	pwrap_read(chip->int_con[0], &val);
	pwrap_read(chip->int_con[1], &val);
	chip->event_mask = event_mask;
}

static void mt6397_set_event_mask(struct mt6397_chip_priv *chip, unsigned int event_mask)
{
	pmic_lock();
	mt6397_set_event_mask_locked(chip, event_mask);
	pmic_unlock();
}

/* this function is only called by generic IRQ framework, and it is always
 * called with pmic_lock held by IRQ framework. */
static void mt6397_irq_mask_unmask_locked(struct irq_data *d, bool enable)
{
	struct mt6397_chip_priv *mt_chip = d->chip_data;
	int hw_irq = d->hwirq;
	u16 port = (hw_irq >> 4) & 1;
	unsigned int val;

	if (enable)
		set_bit(hw_irq, (unsigned long *)&mt_chip->event_mask);
	else
		clear_bit(hw_irq, (unsigned long *)&mt_chip->event_mask);

	if (port) {
		pwrap_write(mt_chip->int_con[1], (mt_chip->event_mask >> 16) & 0xFFFF);
		pwrap_read(mt_chip->int_con[1], &val);
	} else {
		pwrap_write(mt_chip->int_con[0], mt_chip->event_mask & 0xFFFF);
		pwrap_read(mt_chip->int_con[0], &val);
	}
}

static void mt6397_irq_mask_locked(struct irq_data *d)
{
	mt6397_irq_mask_unmask_locked(d, false);
	mdelay(5);
}

static void mt6397_irq_unmask_locked(struct irq_data *d)
{
	mt6397_irq_mask_unmask_locked(d, true);
}

static void mt6397_irq_ack_locked(struct irq_data *d)
{
	struct mt6397_chip_priv *chip = irq_data_get_irq_chip_data(d);

	mt6397_ack_events_locked(chip, 1 << d->hwirq);
}

#ifdef CONFIG_PMIC_IMPLEMENT_UNUSED_EVENT_HANDLERS
static irqreturn_t pwrkey_rstb_int_handler(int irq, void *dev_id)
{
	pr_info("%s:\n", __func__);
	upmu_set_rg_int_en_pwrkey_rstb(0);

	return IRQ_HANDLED;
}

static irqreturn_t hdmi_sifm_int_handler(int irq, void *dev_id)
{
	pr_info("%s:\n", __func__);
	upmu_set_rg_int_en_hdmi_sifm(0);

#ifdef CONFIG_MTK_INTERNAL_MHL_SUPPORT
	vMhlTriggerIntTask();
#endif

	return IRQ_HANDLED;
}

static irqreturn_t hdmi_cec_int_handler(int irq, void *dev_id)
{
	pr_info("%s:\n", __func__);
	upmu_set_rg_int_en_hdmi_cec(0);

	return IRQ_HANDLED;
}

static irqreturn_t vsrmca15_int_handler(int irq, void *dev_id)
{
	pr_info("%s:\n", __func__);
	upmu_set_rg_pwmoc_ck_pdn(1);
	upmu_set_rg_int_en_vsrmca15(0);

	return IRQ_HANDLED;
}

static irqreturn_t vcore_int_handler(int irq, void *dev_id)
{
	pr_info("%s:\n", __func__);
	upmu_set_rg_pwmoc_ck_pdn(1);
	upmu_set_rg_int_en_vcore(0);

	return IRQ_HANDLED;
}

static irqreturn_t vio18_int_handler(int irq, void *dev_id)
{
	pr_info("%s:\n", __func__);
	upmu_set_rg_pwmoc_ck_pdn(1);
	upmu_set_rg_int_en_vio18(0);

	return IRQ_HANDLED;
}

static irqreturn_t vpca7_int_handler(int irq, void *dev_id)
{
	pr_info("%s:\n", __func__);
	upmu_set_rg_pwmoc_ck_pdn(1);
	upmu_set_rg_int_en_vpca7(0);

	return IRQ_HANDLED;
}

static irqreturn_t vsrmca7_int_handler(int irq, void *dev_id)
{
	pr_info("%s:\n", __func__);
	upmu_set_rg_pwmoc_ck_pdn(1);
	upmu_set_rg_int_en_vsrmca7(0);

	return IRQ_HANDLED;
}

static irqreturn_t vdrm_int_handler(int irq, void *dev_id)
{
	pr_info("%s:\n", __func__);
	upmu_set_rg_pwmoc_ck_pdn(1);
	upmu_set_rg_int_en_vdrm(0);

	return IRQ_HANDLED;
}

static irqreturn_t vca15_int_handler(int irq, void *dev_id)
{
	pr_info("%s:\n", __func__);
	upmu_set_rg_pwmoc_ck_pdn(1);
	upmu_set_rg_int_en_vca15(0);

	return IRQ_HANDLED;
}

static irqreturn_t vgpu_int_handler(int irq, void *dev_id)
{
	pr_info("%s:\n", __func__);
	upmu_set_rg_pwmoc_ck_pdn(1);
	upmu_set_rg_int_en_vgpu(0);

	return IRQ_HANDLED;
}

#endif

static irqreturn_t pwrkey_int_handler(int irq, void *dev_id)
{
	unsigned int pwrkey_deb = 0;

	pr_info("%s:\n", __func__);

	pwrkey_deb = upmu_get_pwrkey_deb();

	if (pwrkey_deb == 1) {
		pr_info("[Power/PMIC]" "[pwrkey_int_handler] Release pwrkey\n");
#if defined(CONFIG_MTK_KERNEL_POWER_OFF_CHARGING)
		if (get_boot_mode() == KERNEL_POWER_OFF_CHARGING_BOOT) {
			timer_pos = sched_clock();
			if (timer_pos - timer_pre >= LONG_PWRKEY_PRESS_TIME)
				long_pwrkey_press = true;

			pr_info("[Power/PMIC]" "pos = %ld, pre = %ld, pos-pre = %ld, long_pwrkey_press = %d\r\n",
				timer_pos, timer_pre, timer_pos - timer_pre, long_pwrkey_press);
			if (long_pwrkey_press) {	/* 500ms */
				pr_info("[Power/PMIC]" "Pwrkey Pressed during kpoc, reboot OS\r\n");
				arch_reset(0, NULL);
			}
		}
#endif
		kpd_pwrkey_pmic_handler(0x0);
	} else {
		pr_info("[Power/PMIC][pwrkey_int_handler] Press pwrkey\n");
#if defined(CONFIG_MTK_KERNEL_POWER_OFF_CHARGING)
		if (get_boot_mode() == KERNEL_POWER_OFF_CHARGING_BOOT)
			timer_pre = sched_clock();

#endif
		kpd_pwrkey_pmic_handler(0x1);
	}

	return IRQ_HANDLED;
}

static irqreturn_t homekey_int_handler(int irq, void *dev_id)
{
	pr_info("%s:\n", __func__);

	if (upmu_get_homekey_deb() == 1) {
		pr_info("[Power/PMIC]" "[homekey_int_handler] Release HomeKey\r\n");

		kpd_pmic_rstkey_handler(0x0);

	} else {
		pr_info("[Power/PMIC]" "[homekey_int_handler] Press HomeKey\r\n");

		kpd_pmic_rstkey_handler(0x1);

	}

	return IRQ_HANDLED;
}

static irqreturn_t rtc_int_handler(int irq, void *dev_id)
{
	/* TODO: mark rtc_irq_handler before rtc driver is ready */
/*	rtc_irq_handler(); */
	msleep(100);

	return IRQ_HANDLED;
}

#ifdef CONFIG_MTK_ACCDET
static irqreturn_t accdet_int_handler(int irq, void *dev_id)
{
	kal_uint32 ret = 0;

	pr_info("%s:\n", __func__);

	ret = accdet_irq_handler();
	if (0 == ret)
		pr_info("[Power/PMIC]" "[accdet_int_handler] don't finished\n");
	return IRQ_HANDLED;
}
#endif

static struct mt6397_irq_data mt6397_irqs[] = {
	{
	 .name = "mt6397_pwrkey",
	 .irq_id = RG_INT_STATUS_PWRKEY,
	 .action_fn = pwrkey_int_handler,
	 .enabled = true,
	 .wake_src = true,
	 },
	{
	 .name = "mt6397_homekey",
	 .irq_id = RG_INT_STATUS_HOMEKEY,
	 .action_fn = homekey_int_handler,
	 .enabled = true,
	 },
	{
	 .name = "mt6397_rtc",
	 .irq_id = RG_INT_STATUS_RTC,
	 .action_fn = rtc_int_handler,
	 .enabled = true,
	 .wake_src = true,
	 },
#ifdef CONFIG_MTK_ACCDET
	{
	 .name = "mt6397_accdet",
	 .irq_id = RG_INT_STATUS_ACCDET,
	 .action_fn = accdet_int_handler,
	 .enabled = true,
	 },
#endif
#if defined(CONFIG_MTK_BATTERY_PROTECT)
	{
	 .name = "mt6397_bat_l",
	 .irq_id = RG_INT_STATUS_BAT_L,
	 .action_fn = bat_l_int_handler,
	 .enabled = true,
	 },
	{
	 .name = "mt6397_bat_h",
	 .irq_id = RG_INT_STATUS_BAT_H,
	 .action_fn = bat_h_int_handler,
	 .enabled = true,
	 },
#endif
#ifdef CONFIG_PMIC_IMPLEMENT_UNUSED_EVENT_HANDLERS
	{
	 .name = "mt6397_vca15",
	 .irq_id = RG_INT_STATUS_VCA15,
	 .action_fn = vca15_int_handler,
	 },
	{
	 .name = "mt6397_vgpu",
	 .irq_id = RG_INT_STATUS_VGPU,
	 .action_fn = vgpu_int_handler,
	 },
	{
	 .name = "mt6397_pwrkey_rstb",
	 .irq_id = RG_INT_STATUS_PWRKEY_RSTB,
	 .action_fn = pwrkey_rstb_int_handler,
	 },
	{
	 .name = "mt6397_hdmi_sifm",
	 .irq_id = RG_INT_STATUS_HDMI_SIFM,
	 .action_fn = hdmi_sifm_int_handler,
	 },
	{
	 .name = "mt6397_hdmi_cec",
	 .irq_id = RG_INT_STATUS_HDMI_CEC,
	 .action_fn = hdmi_cec_int_handler,
	 },
	{
	 .name = "mt6397_srmvca15",
	 .irq_id = RG_INT_STATUS_VSRMCA15,
	 .action_fn = vsrmca15_int_handler,
	 },
	{
	 .name = "mt6397_vcore",
	 .irq_id = RG_INT_STATUS_VCORE,
	 .action_fn = vcore_int_handler,
	 },
	{
	 .name = "mt6397_vio18",
	 .irq_id = RG_INT_STATUS_VIO18,
	 .action_fn = vio18_int_handler,
	 },
	{
	 .name = "mt6397_vpca7",
	 .irq_id = RG_INT_STATUS_VPCA7,
	 .action_fn = vpca7_int_handler,
	 },
	{
	 .name = "mt6397_vsram7",
	 .irq_id = RG_INT_STATUS_VSRMCA7,
	 .action_fn = vsrmca7_int_handler,
	 },
	{
	 .name = "mt6397_vdrm",
	 .irq_id = RG_INT_STATUS_VDRM,
	 .action_fn = vdrm_int_handler,
	 },
#endif
};

static inline void mt6397_do_handle_events(struct mt6397_chip_priv *chip, unsigned int events)
{
	int event_hw_irq;

	for (event_hw_irq = __ffs(events); events;
	     events &= ~(1 << event_hw_irq), event_hw_irq = __ffs(events)) {
		int event_irq = irq_find_mapping(chip->domain, 0) + event_hw_irq;

		pr_debug("%s: event=%d\n", __func__, event_hw_irq);

		{
			unsigned long flags;
			/* simulate HW irq */
			local_irq_save(flags);
			generic_handle_irq(event_irq);
			local_irq_restore(flags);
		}
	}
}

static inline void mt6397_set_suspended(struct mt6397_chip_priv *chip, bool suspended)
{
	chip->suspended = suspended;
	smp_wmb();		/* matching barrier is in mt6397_is_suspended */
}

static inline bool mt6397_is_suspended(struct mt6397_chip_priv *chip)
{
	smp_rmb();		/* matching barrier is in mt6397_set_suspended */
	return chip->suspended;
}

static inline bool mt6397_do_irq(struct mt6397_chip_priv *chip)
{
	unsigned int events = mt6397_get_events(chip);

	if (!events)
		return false;

	/* if event happens when it is masked, it is a HW bug,
	 * unless it is a wakeup interrupt */
	if (events & ~(chip->event_mask | chip->wake_mask)) {
		pr_err("%s: PMIC is raising events %08X which are not enabled\n"
		       "\t(mask 0x%lx, wakeup 0x%lx). HW BUG. Stop\n",
		       __func__, events, chip->event_mask, chip->wake_mask);
		pr_err("int ctrl: %08x, status: %08x\n",
		       mt6397_get_event_mask_locked(chip), mt6397_get_events(chip));
		pr_err("int ctrl: %08x, status: %08x\n",
		       mt6397_get_event_mask_locked(chip), mt6397_get_events(chip));
		BUG();
	}

	mt6397_do_handle_events(chip, events);

	return true;
}

static irqreturn_t mt6397_irq(int irq, void *d)
{
	struct mt6397_chip_priv *chip = (struct mt6397_chip_priv *)d;

	while (!mt6397_is_suspended(chip) && mt6397_do_irq(chip))
		continue;

	return IRQ_HANDLED;
}

static void mt6397_irq_bus_lock(struct irq_data *d)
{
	pmic_lock();
}

static void mt6397_irq_bus_sync_unlock(struct irq_data *d)
{
	pmic_unlock();
}

static void mt6397_irq_chip_suspend(struct mt6397_chip_priv *chip)
{
	pmic_lock();

	chip->saved_mask = mt6397_get_event_mask_locked(chip);
	pr_debug("%s: read event mask=%08X\n", __func__, chip->saved_mask);
	mt6397_set_event_mask_locked(chip, chip->wake_mask);
	pr_debug("%s: write event mask= 0x%lx\n", __func__, chip->wake_mask);

	pmic_unlock();
}

static void mt6397_irq_chip_resume(struct mt6397_chip_priv *chip)
{
	struct mt_wake_event *we = spm_get_wakeup_event();
	u32 events = mt6397_get_events(chip);
	int event = __ffs(events);

	mt6397_set_event_mask(chip, chip->saved_mask);

	if (events && we && we->domain && !strcmp(we->domain, "EINT") && we->code == chip->irq_hw_id) {
		spm_report_wakeup_event(&mt6397_event, event);
		chip->wakeup_event = events;
	}
}

static int mt6397_irq_set_wake_locked(struct irq_data *d, unsigned int on)
{
	struct mt6397_chip_priv *chip = irq_data_get_irq_chip_data(d);

	if (on)
		set_bit(d->hwirq, (unsigned long *)&chip->wake_mask);
	else
		clear_bit(d->hwirq, (unsigned long *)&chip->wake_mask);
	return 0;
}

static struct irq_chip mt6397_irq_chip = {
	.name = "mt6397-irqchip",
	.irq_ack = mt6397_irq_ack_locked,
	.irq_mask = mt6397_irq_mask_locked,
	.irq_unmask = mt6397_irq_unmask_locked,
	.irq_set_wake = mt6397_irq_set_wake_locked,
	.irq_bus_lock = mt6397_irq_bus_lock,
	.irq_bus_sync_unlock = mt6397_irq_bus_sync_unlock,
};

static int mt6397_irq_init(struct mt6397_chip_priv *chip)
{
	int i;
	int ret;

	chip->domain = irq_domain_add_linear(chip->dev->of_node->parent,
		MT6397_IRQ_NR, &irq_domain_simple_ops, chip);
	if (!chip->domain) {
		dev_err(chip->dev, "could not create irq domain\n");
		return -ENOMEM;
	}

	for (i = 0; i < chip->num_int; i++) {
		int virq = irq_create_mapping(chip->domain, i);

		irq_set_chip_and_handler(virq, &mt6397_irq_chip,
			handle_level_irq);
		irq_set_chip_data(virq, chip);
		set_irq_flags(virq, IRQF_VALID);

	};

	mt6397_set_event_mask(chip, 0);
	pr_debug("%s: PMIC: event_mask=%08X; events=%08X\n",
		 __func__, mt6397_get_event_mask(chip), mt6397_get_events(chip));

	ret = request_threaded_irq(chip->irq, NULL, mt6397_irq,
				    IRQF_ONESHOT, mt6397_irq_chip.name, chip);
	if (ret < 0) {
		pr_info("%s: PMIC master irq request err: %d\n", __func__, ret);
		goto err_free_domain;
	}

	irq_set_irq_wake(chip->irq, true);
	return 0;
 err_free_domain:
	irq_domain_remove(chip->domain);
	return ret;
}

static int mt6397_irq_handler_init(struct mt6397_chip_priv *chip)
{
	int i;
	/*AP:
	 * I register all the non-default vectors,
	 * and disable all vectors that were not enabled by original code;
	 * threads are created for all non-default vectors.
	 */
	for (i = 0; i < ARRAY_SIZE(mt6397_irqs); i++) {
		int ret, irq;
		struct mt6397_irq_data *data = &mt6397_irqs[i];

		irq = irq_find_mapping(chip->domain, data->irq_id);
		ret = request_threaded_irq(irq, NULL, data->action_fn,
					   IRQF_TRIGGER_HIGH | IRQF_ONESHOT, data->name, chip);
		if (ret) {
			pr_info("%s: failed to register irq=%d (%d); name='%s'; err: %d\n",
				__func__, irq, data->irq_id, data->name, ret);
			continue;
		}
		if (!data->enabled)
			disable_irq(irq);
		if (data->wake_src)
			irq_set_irq_wake(irq, 1);
		pr_info("%s: registered irq=%d (%d); name='%s'; enabled: %d\n",
			__func__, irq, data->irq_id, data->name, data->enabled);
	}
	return 0;
}

static int mt6397_syscore_suspend(void)
{
	mt6397_irq_chip_suspend(mt6397_chip);
	return 0;
}

static void mt6397_syscore_resume(void)
{
	mt6397_irq_chip_resume(mt6397_chip);
}

static struct syscore_ops mt6397_syscore_ops = {
	.suspend = mt6397_syscore_suspend,
	.resume = mt6397_syscore_resume,
};

static int pmic_mt6397_probe(struct platform_device *pdev)
{
	struct mt6397_chip_priv *chip;
	int ret;
	struct mt6397_chip *mt6397 = dev_get_drvdata(pdev->dev.parent);

	pr_debug("[Power/PMIC] ******** MT6397 pmic driver probe!! ********\n");

	/* enable PWRKEY/HOMEKEY posedge detected interrupt */
	upmu_set_rg_pwrkey_int_sel(1);
	upmu_set_rg_homekey_int_sel(1);
	upmu_set_rg_homekey_puen(1);

	chip = kzalloc(sizeof(struct mt6397_chip_priv), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;
	chip->irq = mt6397->irq; /* hw irq of EINT */
	chip->irq_hw_id = (int)irqd_to_hwirq(irq_get_irq_data(mt6397->irq)); /* EINT num */

	chip->num_int = 32;
	chip->int_con[0] = INT_CON0;
	chip->int_con[1] = INT_CON1;
	chip->int_stat[0] = INT_STATUS0;
	chip->int_stat[1] = INT_STATUS1;

	dev_set_drvdata(chip->dev, chip);

	device_init_wakeup(chip->dev, true);
	pr_debug("[Power/PMIC][PMIC_EINT_SETTING] Done\n");

	ret = mt6397_irq_init(chip);
	if (ret)
		return ret;

	ret = mt6397_irq_handler_init(chip);
	if (ret)
		return ret;

	pwrkey_wev.irq = irq_find_mapping(chip->domain, RG_INT_STATUS_PWRKEY);
	rtc_wev.irq = irq_find_mapping(chip->domain, RG_INT_STATUS_RTC);
	charger_wev.irq = irq_find_mapping(chip->domain, RG_INT_STATUS_CHRDET);
	spm_register_wakeup_event(&pwrkey_wev);
	spm_register_wakeup_event(&rtc_wev);
	spm_register_wakeup_event(&charger_wev);

	mt6397_chip = chip;
	register_syscore_ops(&mt6397_syscore_ops);

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
