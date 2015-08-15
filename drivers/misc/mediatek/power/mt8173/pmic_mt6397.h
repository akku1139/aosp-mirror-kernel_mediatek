#ifndef _PMIC_MT6397_H_
#define _PMIC_MT6397_H_

extern void kpd_pwrkey_pmic_handler(unsigned long pressed);
extern void kpd_pmic_rstkey_handler(unsigned long pressed);
extern int accdet_irq_handler(void);
extern void accdet_auxadc_switch(int enable);

struct mt6397_event_stat {
	u64 last;
	int count;
	bool blocked:1;
	bool wake_blocked:1;
};

struct mt6397_chip_priv {
	struct device *dev;
	struct irq_domain *domain;
	unsigned long event_mask;
	unsigned long wake_mask;
	unsigned int saved_mask;
	u16 int_con[2];
	u16 int_stat[2];
	int irq;
	int irq_base;
	int num_int;
	int irq_hw_id;
	bool suspended:1;
	unsigned int wakeup_event;
	struct mt6397_event_stat stat[32];
};

struct mt6397_irq_data {
	const char *name;
	unsigned int irq_id;
	 irqreturn_t (*action_fn)(int irq, void *dev_id);
	bool enabled:1;
	bool wake_src:1;
};

#endif				/* _PMIC_MT6397_H_ */
