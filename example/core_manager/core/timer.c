#include "core.h"
#include "uart.h"
#include <stdint.h>

#define MANAGER_CHANNEL         1
#define API_CHANNEL             2
#define TIMER_IRQ_CHANNEL       6

#if defined(CONFIG_PLAT_MAAXBOARD)
volatile uint32_t *gpt_base_vaddr;

/* Peripheral base/regs from i.MX8MM RM (GPT block) */
#define GPT_CR                  0u  /* Control Register */
#define GPT_PR                  1u  /* Prescaler Register */
#define GPT_SR                  2u  /* Status Register (W1C bits) */
#define GPT_IR                  3u  /* Interrupt Register (mask bits) */
#define GPT_OCR1                4u
#define GPT_OCR2                5u
#define GPT_OCR3                6u
#define GPT_ICR1                7u
#define GPT_ICR2                8u
#define GPT_CNT                 9u

#define GPT_STATUS_REG_CLEAR    0x3Fu

/* Your board’s clocking fed GPT with 12 MHz in your existing setup. */
#define GPT_FREQ_MHZ            12u

static void plat_timer_setup(void)
{
    /* Disable & clear */
    gpt_base_vaddr[GPT_CR] = 0;
    gpt_base_vaddr[GPT_SR] = GPT_STATUS_REG_CLEAR;

    /* Software reset */
    gpt_base_vaddr[GPT_CR] = (1u << 15);
    while (gpt_base_vaddr[GPT_CR] & (1u << 15)) { }

    /* No prescale; 1 Hz via OCR1 */
    gpt_base_vaddr[GPT_PR] = 0;
    gpt_base_vaddr[GPT_OCR1] = 1000u * 1000u * GPT_FREQ_MHZ;

    /* Peripheral clock, restart mode off, don’t enable yet */
    gpt_base_vaddr[GPT_CR] = (0u << 9) | (1u << 6);

    /* Mask interrupts for now */
    gpt_base_vaddr[GPT_IR] = 0;
}

static void plat_timer_enable_irq(void)
{
    /* Ensure clocks/domain are up like on your i.MX flow */
    microkit_mr_set(0, CORES_RESTART_PMU);
    microkit_ppcall(API_CHANNEL, microkit_msginfo_new(0, 1));

    gpt_base_vaddr[GPT_IR] = (1u << 0);      /* Compare1 */
    gpt_base_vaddr[GPT_CR] |= (1u << 0);     /* Enable GPT */
}

static void plat_timer_disable_irq(void)
{
    gpt_base_vaddr[GPT_IR] = 0;
    gpt_base_vaddr[GPT_CR] &= ~(1u << 0);    /* Disable GPT */
}

static void plat_timer_ack_irq(void)
{
    /* W1C – write back the bits we read to clear */
    uint32_t sr = gpt_base_vaddr[GPT_SR];
    gpt_base_vaddr[GPT_SR] = sr;
}

#elif defined(CONFIG_PLAT_ZYNQMP)
volatile uint8_t *ttc_base_vaddr;

/* TTC register offsets (UG1087: TTC Module) */
#define TTC_CLK_CTRL_1          0x00
#define TTC_CNT_CTRL_1          0x0C
#define TTC_CNT_VAL_1           0x18
#define TTC_INTERVAL_1          0x24
#define TTC_INT_STATUS_1        0x54  /* clear-on-read */
#define TTC_INT_ENABLE_1        0x60
#define TTC_EVENT_CTRL_1        0x6C  /* (unused) */

#define CNTCTRL_INTERVAL_MODE   (1u << 1)
#define CNTCTRL_DISABLE         (1u << 0)
#define CNTCTRL_RESET           (1u << 4)

#define TTC_IRQ_INTERVAL        (1u << 0)

/* Nominal PS low-power domain bus clock ~100 MHz for TTC blocks */
#define TTC_CLK_HZ              100000000u

static inline void ttc_w(uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(ttc_base_vaddr + off) = v;
}
static inline uint32_t ttc_r(uint32_t off) {
    return *(volatile uint32_t *)(ttc_base_vaddr + off);
}

static void plat_timer_setup(void) {
    /* Stop counter, clear pending */
    ttc_w(TTC_CNT_CTRL_1, CNTCTRL_DISABLE);
    (void)ttc_r(TTC_INT_STATUS_1); /* read-to-clear */

    /* No prescale; 1 Hz via interval register */
    ttc_w(TTC_CLK_CTRL_1, 0);
    ttc_w(TTC_INTERVAL_1, TTC_CLK_HZ);

    /* Interval mode, disabled for now */
    ttc_w(TTC_CNT_CTRL_1, CNTCTRL_INTERVAL_MODE | CNTCTRL_DISABLE);

    /* Keep interrupts masked */
    ttc_w(TTC_INT_ENABLE_1, 0);
}

static void plat_timer_enable_irq(void) {
    /* Match your existing pattern: ensure clocks/power domain are up */
    microkit_mr_set(0, CORES_RESTART_PMU);
    microkit_ppcall(API_CHANNEL, microkit_msginfo_new(0, 1));

    ttc_w(TTC_INT_ENABLE_1, TTC_IRQ_INTERVAL);
    /* Reset then enable (DIS=0) in interval mode */
    ttc_w(TTC_CNT_CTRL_1, CNTCTRL_INTERVAL_MODE | CNTCTRL_RESET);
}

static void plat_timer_disable_irq(void) {
    ttc_w(TTC_INT_ENABLE_1, 0);
    ttc_w(TTC_CNT_CTRL_1, CNTCTRL_INTERVAL_MODE | CNTCTRL_DISABLE);
}

static void plat_timer_ack_irq(void) {
    /* clear-on-read */
    (void)ttc_r(TTC_INT_STATUS_1);
}

#else
#error "Select CONFIG_PLAT_MAAXBOARD or CONFIG_PLAT_ZYNQMP"
#endif

void init(void) {
    plat_timer_setup();
}

void notified(microkit_channel ch) {
    if (ch != TIMER_IRQ_CHANNEL) {
        uart_puts("[Timer Driver]: Unexpected notification: ");
        uart_put64(ch);
        uart_puts("\n");
        return;
    }

    plat_timer_ack_irq();
    microkit_notify(MANAGER_CHANNEL);
    microkit_irq_ack(ch);
}

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo) {
    if (ch != MANAGER_CHANNEL) {
        uart_puts("[Timer Driver]: Unexpected PPC: ");
        uart_put64(ch);
        uart_puts("\n");
        return microkit_msginfo_new(0, 0);
    }

    seL4_Bool enable = microkit_mr_get(0);
    if (enable) {
        plat_timer_enable_irq();
        uart_puts("[Timer Driver]: Automatic mode enabled (1 Hz)\n");
    } else {
        plat_timer_disable_irq();
        uart_puts("[Timer Driver]: Manual mode enabled (no timer IRQs)\n");
    }
    return microkit_msginfo_new(0, 0);
}
