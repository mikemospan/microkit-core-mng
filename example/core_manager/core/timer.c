#include "core.h"
#include "uart.h"

// ============================================================================
// Constants
// ============================================================================

#define MANAGER_CHANNEL         1
#define API_CHANNEL             2

// Timer configuration
#define TIMER_IRQ_CHANNEL       6
#define GPT_FREQ                12u  // 12 MHz peripheral clock

// GPT register offsets (indexed as uint32_t array)
#define GPT_CR                  0    // Control Register
#define GPT_PR                  1    // Prescaler Register
#define GPT_SR                  2    // Status Register
#define GPT_IR                  3    // Interrupt Register
#define GPT_OCR1                4    // Output Compare Register 1
#define GPT_OCR2                5    // Output Compare Register 2
#define GPT_OCR3                6    // Output Compare Register 3
#define GPT_ICR1                7    // Input Capture Register 1
#define GPT_ICR2                8    // Input Capture Register 2
#define GPT_CNT                 9    // Counter Register

#define GPT_STATUS_REG_CLEAR    0x3F // Clear all status bits

// ============================================================================
// Global Variables
// ============================================================================

// Memory-mapped GPT registers
volatile uint32_t *gpt_base_vaddr;

// ============================================================================
// Function Prototypes
// ============================================================================

// Timer handling
static void setup_timer(void);
static void handle_timer_irq(void);
static void enable_timer_interrupts(void);
static void disable_timer_interrupts(void);

// ============================================================================
// Microkit API Implementation
// ============================================================================

/**
 * Initialise the timer.
 */
void init(void) {
    setup_timer();
}

/**
 * Handle UART interrupts for user input, and timer interrupts if auto is enabled.
 */
void notified(microkit_channel ch) {
    if (ch != TIMER_IRQ_CHANNEL) {
        uart_puts("[Timer Driver]: Received unexpected notification: ");
        uart_put64(ch);
        uart_puts("\n");
        return;
    }

    handle_timer_irq();
    microkit_irq_ack(ch);
}

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo) {
    if (ch != MANAGER_CHANNEL) {
        uart_puts("[Timer Driver]: Received unexpected PPC: ");
        uart_put64(ch);
        uart_puts("\n");
        return microkit_msginfo_new(0, 0);
    }

    seL4_Bool enable = microkit_mr_get(0);
    if (enable) {
        enable_timer_interrupts();
    } else {
        disable_timer_interrupts();
    }
    
    return microkit_msginfo_new(0, 0);
}


// ============================================================================
// Timer Management
// ============================================================================

/**
 * Clear the status bits and notify the core manager.
 */
static void handle_timer_irq(void) {
    uint32_t sr = gpt_base_vaddr[GPT_SR];
    gpt_base_vaddr[GPT_SR] = sr;
    microkit_notify(MANAGER_CHANNEL);
}

/**
 * Configure and start the GPT timer for 1-second periodic interrupts.
 */
static void setup_timer(void) {
    // Disable GPT and clear pending interrupts
    gpt_base_vaddr[GPT_CR] = 0;
    gpt_base_vaddr[GPT_SR] = GPT_STATUS_REG_CLEAR;

    // Software reset
    gpt_base_vaddr[GPT_CR] = (1 << 15);
    while (gpt_base_vaddr[GPT_CR] & (1 << 15));

    // No prescaler
    gpt_base_vaddr[GPT_PR] = 0;

    // 1-second compare interval
    gpt_base_vaddr[GPT_OCR1] = 1 * 1000 * 1000 * GPT_FREQ;

    // Peripheral clock, restart mode (but don't enable yet)
    gpt_base_vaddr[GPT_CR] =
        (0 << 9) |  // Restart mode
        (1 << 6);   // Peripheral clock source only

    // Don't enable interrupts yet — manual mode by default
    gpt_base_vaddr[GPT_IR] = 0;
}

static void enable_timer_interrupts(void) {
    microkit_mr_set(0, CORES_RESTART_PMU);
    microkit_ppcall(API_CHANNEL, microkit_msginfo_new(0, 1));
    
    gpt_base_vaddr[GPT_IR] = (1 << 0);  // Enable Compare1 interrupt
    gpt_base_vaddr[GPT_CR] |= (1 << 0); // Enable GPT
    uart_puts("[Timer Driver]: Automatic mode enabled (timer interrupts active)\n");
}

static void disable_timer_interrupts(void) {
    gpt_base_vaddr[GPT_IR] = 0;         // Disable interrupts
    gpt_base_vaddr[GPT_CR] &= ~(1 << 0); // Stop GPT
    uart_puts("[Timer Driver]: Manual mode enabled (no timer interrupts)\n");
}
