#include "core.h"
#include "uart.h"

// ============================================================================
// Constants
// ============================================================================

#define API_CHANNEL     1
#define TIMER_CHANNEL   3
#define DELETE          127

// ============================================================================
// Global Variables
// ============================================================================

// Tracks which PDs are running on each core
char core_pds[NUM_CPUS][MAX_PDS][MICROKIT_PD_NAME_LENGTH];

// Command input buffer
char *cmd_buffer;
int cmd_len = 0;

// Current core running the Monitor PD
uint8_t monitor_core = 0;

// Start in manual mode (no timer interrupts)
seL4_Bool auto_mode = 0;

// Counter to track number of cores on
uint8_t cores_on = NUM_CPUS;

// ============================================================================
// Function Prototypes
// ============================================================================

// String utilities
static int str_eq(const char *a, const char *b);
static char *skip_ws(char *p);
static char *next_token(char **p);
static int str_to_int(const char *s, seL4_Bool *success);

// Core management
static seL4_Word send_core_command(Instruction cmd, uint8_t core_id, uint8_t pd_id);
static inline seL4_Bool migrate_pd(uint8_t from_core, uint8_t to_core, uint8_t pd_id);
static inline seL4_Bool migrate_monitor(uint8_t to_core);
static void dump_core(uint8_t core);

// Command handling
static void handle_user_input(char input);
static void execute_command(char *cmd);
static void print_help(void);

// ============================================================================
// Microkit API Implementation
// ============================================================================

/**
 * Initialise the core manager.
 * - Sets up UART and clears the command buffer.
 */
void init(void) {
    uart_init();
    cmd_buffer[0] = '\0';
    cmd_len = 0;
}

static int find_most_suitable_core(int exclude_core, uint64_t *utils) {
    int best_core = -1;
    uint64_t best_util = 0;

    for (int core_id = 0; core_id < NUM_CPUS; core_id++) {
        if (core_id == exclude_core) {
            continue;
        }

        seL4_Word status = send_core_command(CORE_STATUS, core_id, 0);
        if (status != CORE_ON) {
            continue;
        }

        // Pick the most utilised core under 90%
        if (utils[core_id] > best_util && utils[core_id] < 900000000ULL) {
            best_core = core_id;
            best_util = utils[core_id];
        }
    }

    return best_core;
}

/**
 * Handle UART interrupts for user input, and timer interrupts if auto is enabled.
 */
void notified(microkit_channel ch) {
    if (ch == UART_IRQ_CH) {
        // Handle platform-specific IRQ acknowledgment
        uart_handle_irq();

        // Read character and echo it back
        char input = uart_getc();
        if (input == '\n') {
            uart_puts("\n");
        } else {
            uart_putc(input);
        }

        microkit_irq_ack(ch);
        handle_user_input(input);
    }
#if CONFIG_BENCHMARK
    else if (ch == TIMER_CHANNEL) {
        microkit_mr_set(0, CORES_QUERY);
        microkit_ppcall(API_CHANNEL, microkit_msginfo_new(0, 1));

        // Read utilisation and store locally
        uint64_t utils[NUM_CPUS];
        for (int i = 0; i < NUM_CPUS; i++) {
            utils[i] = microkit_mr_get(i);
        }

        // --- Automatic migration logic ---
        const uint64_t THRESHOLD = 100000000; // 10% of 1 second scaled to 1e9 cycles
        for (int i = 0; i < NUM_CPUS; i++) {
            seL4_Word status = send_core_command(CORE_STATUS, i, 0);
            if (utils[i] < THRESHOLD && status == 0 && cores_on > 1) {
                int to_core = -1;
                for (int pd_id = 0; pd_id < MAX_PDS; pd_id++) {
                    if (core_pds[i][pd_id][0] == '\0') continue;

                    to_core = find_most_suitable_core(i, utils);
                    if (to_core < 0) {
                        break;
                    }

                    migrate_pd(i, to_core, pd_id);
                }

                if (to_core >= 0 && i == monitor_core) {
                    migrate_monitor(to_core);
                }
                
                if (to_core >= 0) {
                    uart_puts("[Core Manager]: Core ");
                    uart_put64(i);
                    uart_puts(" at ");
                    uart_putfloat(utils[i] * 100, 1000000000, 2);
                    uart_puts("% utilisation, migrated PDs and powered down core.\n");

                    send_core_command(CORE_OFF, i, 0);
                    cores_on--;
                }
            }
        }
    }
#endif
    else {
        uart_puts("[Core Manager]: Received unexpected notification: ");
        uart_put64(ch);
        uart_puts("\n");
    }
}

// ============================================================================
// Command Input Handling
// ============================================================================

/**
 * Process a single character of user input.
 * Handles command execution on newline and backspace for editing.
 */
static void handle_user_input(char input) {
    if (input == '\r' || input == '\n') {
        uart_puts("\n");
        execute_command(cmd_buffer);
        cmd_len = 0;
        cmd_buffer[0] = '\0';
    } else if (input == DELETE) {
        // Handle backspace
        if (cmd_len > 0) {
            cmd_len--;
            cmd_buffer[cmd_len] = '\0';
            uart_puts("\b \b");  // Erase character from terminal
        }
    } else {
        // Append character to buffer
        cmd_buffer[cmd_len++] = input;
        cmd_buffer[cmd_len] = '\0';
    }
}

// ============================================================================
// Command Execution
// ============================================================================

/**
 * Parse and execute a user command.
 * Supports core management operations like migrate, status, on/off, etc.
 */
static void execute_command(char *cmd) {
    char *command = next_token(&cmd);
    if (!command) {
        return;
    }

    seL4_Word err = 0;
    seL4_Bool parse_success;

    if (str_eq(command, "help")) {
        print_help();
    } else if (str_eq(command, "auto")) {
        microkit_mr_set(0, 1);
        microkit_ppcall(TIMER_CHANNEL, microkit_msginfo_new(0, 1));
        auto_mode = 1;
    } else if (str_eq(command, "manual")) {
        microkit_mr_set(0, 0);
        microkit_ppcall(TIMER_CHANNEL, microkit_msginfo_new(0, 1));
        auto_mode = 0;
    } else if (str_eq(command, "status")) {
        char *arg = next_token(&cmd);
        if (arg) {
            int core_id = str_to_int(arg, &parse_success);
            if (!parse_success || core_id < 0 || core_id >= NUM_CPUS) {
                uart_puts("Error: core_id must be a number between 0 and ");
                uart_put64(NUM_CPUS - 1);
                uart_puts("\n");
            } else {
                seL4_Word status = send_core_command(CORE_STATUS, core_id, 0);
                const char *status_str;
                if (status == CORE_ON) {
                    status_str = "ON";
                } else if (status == CORE_OFF) {
                    status_str = "OFF";
                } else if (status == CORE_POWERDOWN) {
                    status_str = "POWERDOWN";
                } else if (status == CORE_STANDBY) {
                    status_str = "STANDBY";
                } else {
                    status_str = "PENDING";
                }
                uart_puts("Core ");
                uart_put64(core_id);
                uart_puts(" is ");
                uart_puts(status_str);
                uart_puts("\n");
            }
        } else {
            uart_puts("Usage: status <core_id>\n");
        }
    } else if (str_eq(command, "dump")) {
        char *arg = next_token(&cmd);
        if (arg) {
            int core_id = str_to_int(arg, &parse_success);
            if (!parse_success || core_id < 0 || core_id >= NUM_CPUS) {
                uart_puts("Error: core_id must be a number between 0 and ");
                uart_put64(NUM_CPUS - 1);
                uart_puts("\n");
            } else {
                dump_core(core_id);
            }
        } else {
            uart_puts("Usage: dump <core_id>\n");
        }
    } else if (str_eq(command, "migrate")) {
        char *pd_arg = next_token(&cmd);
        char *core_arg = next_token(&cmd);

        if (pd_arg && str_eq(pd_arg, "monitor") && core_arg) {
            // Migrate the Monitor PD
            int core_id = str_to_int(core_arg, &parse_success);
            if (!parse_success || core_id < 0 || core_id >= NUM_CPUS) {
                uart_puts("Error: core_id must be a number between 0 and ");
                uart_put64(NUM_CPUS - 1);
                uart_puts("\n");
            } else {
                err = migrate_monitor(core_id);
            }
        } else if (pd_arg && core_arg) {
            // Migrate a regular PD
            int pd_id = str_to_int(pd_arg, &parse_success);
            if (!parse_success || pd_id < 0 || pd_id >= MAX_PDS) {
                uart_puts("Error: pd_id must be a number between 0 and ");
                uart_put64(MAX_PDS - 1);
                uart_puts("\n");
            } else {
                int core_id = str_to_int(core_arg, &parse_success);
                if (!parse_success || core_id < 0 || core_id >= NUM_CPUS) {
                    uart_puts("Error: core_id must be a number between 0 and ");
                    uart_put64(NUM_CPUS - 1);
                    uart_puts("\n");
                } else {
                    // Find which core currently has this PD
                    seL4_Bool found = 0;
                    for (int c = 0; c < NUM_CPUS; c++) {
                        if (core_pds[c][pd_id][0] != '\0') {
                            err = migrate_pd(c, core_id, pd_id);
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        uart_puts("Error: PD ");
                        uart_put64(pd_id);
                        uart_puts(" not found on any core\n");
                    }
                }
            }
        } else {
            uart_puts("Usage: migrate <pd_id> <core> OR migrate monitor <core>\n");
        }
    } else if (str_eq(command, "off")) {
        char *arg = next_token(&cmd);
        if (arg) {
            int core_id = str_to_int(arg, &parse_success);
            if (!parse_success || core_id < 0 || core_id >= NUM_CPUS) {
                uart_puts("Error: core_id must be a number between 0 and ");
                uart_put64(NUM_CPUS - 1);
                uart_puts("\n");
            } else {
                err = send_core_command(CORE_OFF, core_id, 0);
            }
        } else {
            uart_puts("Usage: off <core_id>\n");
        }
    } else if (str_eq(command, "powerdown")) {
        char *arg = next_token(&cmd);
        if (arg) {
            int core_id = str_to_int(arg, &parse_success);
            if (!parse_success || core_id < 0 || core_id >= NUM_CPUS) {
                uart_puts("Error: core_id must be a number between 0 and ");
                uart_put64(NUM_CPUS - 1);
                uart_puts("\n");
            } else {
                err = send_core_command(CORE_POWERDOWN, core_id, 0);
            }
        } else {
            uart_puts("Usage: powerdown <core_id>\n");
        }
    } else if (str_eq(command, "standby")) {
        char *arg = next_token(&cmd);
        if (arg) {
            int core_id = str_to_int(arg, &parse_success);
            if (!parse_success || core_id < 0 || core_id >= NUM_CPUS) {
                uart_puts("Error: core_id must be a number between 0 and ");
                uart_put64(NUM_CPUS - 1);
                uart_puts("\n");
            } else {
                err = send_core_command(CORE_STANDBY, core_id, 0);
            }
        } else {
            uart_puts("Usage: standby <core_id>\n");
        }
    } else if (str_eq(command, "on")) {
        char *arg = next_token(&cmd);
        if (arg) {
            int core_id = str_to_int(arg, &parse_success);
            if (!parse_success || core_id < 0 || core_id >= NUM_CPUS) {
                uart_puts("Error: core_id must be a number between 0 and ");
                uart_put64(NUM_CPUS - 1);
                uart_puts("\n");
            } else {
                err = send_core_command(CORE_ON, core_id, 0);
            }
        } else {
            uart_puts("Usage: on <core_id>\n");
        }
    } else {
        uart_puts("Unknown command. Type 'help' for a list of commands.\n");
    }

    if (err) {
        uart_puts("Core Manager API request failed.\n");
    }
}

// ============================================================================
// Core Command Interface
// ============================================================================

/**
 * Send a command to the Core Manager API via protected procedure call.
 * @return Response from Core Manager API, usually success or failure.
 */
static seL4_Word send_core_command(Instruction cmd, uint8_t core_id, uint8_t pd_id) {
    microkit_mr_set(0, cmd);
    microkit_mr_set(1, core_id);
    microkit_mr_set(2, pd_id);

    microkit_ppcall(API_CHANNEL, microkit_msginfo_new(0, 3));
    return microkit_mr_get(0);
}

/**
 * Migrate a PD from one core to another.
 * Updates internal tracking and sends migration command to Core Manager.
 */
static inline seL4_Bool migrate_pd(uint8_t from_core, uint8_t to_core, uint8_t pd_id) {
    seL4_Bool err = 1;
    if (core_pds[from_core][pd_id][0] != '\0') {
        err = send_core_command(CORE_MIGRATE, to_core, pd_id);
    }

    if (!err) {
        // Update tracking arrays
        memcpy(core_pds[to_core][pd_id], core_pds[from_core][pd_id], MICROKIT_PD_NAME_LENGTH);
        core_pds[from_core][pd_id][0] = '\0';
    }
    
    return err;
}

/**
 * Migrate a PD from one core to another.
 * Updates internal tracking and sends migration command to Core Manager.
 */
static inline seL4_Bool migrate_monitor(uint8_t to_core) {
    seL4_Bool err = send_core_command(CORE_MIGRATE_MONITOR, to_core, 0);
    if (!err) {
        monitor_core = to_core;
    }
    return err;
}

// ============================================================================
// Display Helpers
// ============================================================================

/**
 * Display all PDs currently running on a specific core.
 */
static void dump_core(uint8_t core) {
    if (core >= NUM_CPUS) {
        uart_puts("Error: Invalid core ID\n");
        return;
    }

    uart_puts("=== Core ");
    uart_put64(core);
    uart_puts(" Protection Domains ===\n");
    uart_puts("PD ID\tName\n");
    uart_puts("----------------------\n");

    // Show Monitor if it's on this core
    if (core == monitor_core) {
        uart_puts("N/A\tMicrokit Monitor\n");
    }

    // Show all other PDs
    for (int pd_id = 0; pd_id < MAX_PDS; pd_id++) {
        char *name = core_pds[core][pd_id];
        if (name[0] != '\0') {
            uart_put64(pd_id);
            uart_puts("\t");
            uart_puts(name);
            uart_puts("\n");
        }
    }

    uart_puts("=== End ===\n");
}

/**
 * Print help message with all available commands.
 */
static void print_help(void) {
    uart_puts(
        "\n=== CORE MANAGEMENT COMMANDS ===\n"
        "help                     - Show this help message\n"
        "auto                     - Enable automatic mode (start timer interrupts)\n"
        "manual                   - Enable manual mode (stop timer interrupts)\n\n"
        "status <core_id>         - View the status of a core\n"
        "dump <core_id>           - Dump the protection domains on a core\n"
        "migrate <pd_id> <core>   - Migrate a protection domain to a core\n"
        "migrate monitor <core>   - Migrate the monitor to a specific core\n"
        "off <core_id>            - Turn off a core\n"
        "powerdown <core_id>      - Power down a core\n"
        "standby <core_id>        - Put a core in standby mode\n"
        "on <core_id>             - Turn on a core\n"
    );
}

// ============================================================================
// String Utilities
// ============================================================================

/**
 * Compare two strings for equality.
 * @return 1 if equal, 0 otherwise
 */
static int str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

/**
 * Skip whitespace characters in a string.
 * @return Pointer to first non-whitespace character
 */
static char *skip_ws(char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    return p;
}

/**
 * Extract the next token from a string.
 * Modifies the input string by null-terminating the token.
 * @param p Pointer to string pointer (updated to point after the token)
 * @return Pointer to token, or NULL if no token found
 */
static char *next_token(char **p) {
    char *start = skip_ws(*p);
    if (*start == '\0') {
        *p = start;
        return seL4_Null;
    }

    char *end = start;
    while (*end && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n') {
        end++;
    }

    if (*end) {
        *end = '\0';
        end++;
    }
    *p = end;
    return start;
}

/**
 * Convert a string to a positive integer.
 * @param s String to convert
 * @param success Output parameter indicating if conversion succeeded
 * @return Converted integer value (0 if conversion failed)
 */
static int str_to_int(const char *s, seL4_Bool *success) {
    if (!s || *s == '\0') {
        *success = 0;
        return 0;
    }

    // Reject negative numbers
    if (*s == '-') {
        *success = 0;
        return 0;
    }

    // Check that we have at least one digit
    if (*s < '0' || *s > '9') {
        *success = 0;
        return 0;
    }

    int val = 0;

    // Convert digits
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }

    // Check for trailing non-numeric characters
    if (*s != '\0') {
        *success = 0;
        return 0;
    }

    *success = 1;
    return val;
}
