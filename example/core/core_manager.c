#include "core.h"
#include "uart.h"

#define API_CHANNEL 1
#define BACKSPACE 127

// Each entry corresponds to a core and the pds that it is running
#define MAX_PDS 63
char core_pds[NUM_CPUS][MAX_PDS][MICROKIT_PD_NAME_LENGTH];

// Command buffer for user input
char *cmd_buffer;
int cmd_len = 0;

// The core the initial task (Monitor) is currently on
uint8_t monitor_core = 0;

// Function prototypes
static int str_eq(const char *a, const char *b);
static char *skip_ws(char *p);
static char *next_token(char **p);
static int str_to_int(const char *s);
static void execute_command(char *cmd);
static void handle_user_input(char input);
static seL4_Bool send_core_command(Instruction cmd, uint8_t core_id, uint8_t pd_id);
static void dump_core(uint8_t core);
static void print_help(void);
static inline seL4_Bool migrate_pd(uint8_t from_core, uint8_t to_core, uint8_t pd_id);

void init(void) {
    uart_init();
    cmd_buffer[0] = '\0';
    cmd_len = 0;

    /* Migrate all worker PDs to relevant cores */
    migrate_pd(0, 1, 2);
    migrate_pd(0, 2, 3);
    migrate_pd(0, 3, 4);
}

void notified(microkit_channel ch) {
    if (ch != UART_IRQ_CH) {
        uart_puts("[Core Manager]: Received unexpected notification: ");
        uart_put64(ch);
        uart_putc('\n');
        return;
    }

    char input = uart_getchar();
    uart_handle_irq();
    uart_putc(input);
    
    handle_user_input(input);
    microkit_irq_ack(ch);
}

static void handle_user_input(char input) {
    if (input == '\r' || input == '\n') {
        // Execute command on enter
        execute_command(cmd_buffer);
        cmd_len = 0;
        cmd_buffer[0] = '\0';
    }  else if (input == BACKSPACE) {
        // Handle backspace
        if (cmd_len > 0) {
            cmd_len--;
            cmd_buffer[cmd_len] = '\0';
            uart_puts("\b \b"); // Erase character on terminal
        }
    } else {
        // Add character to buffer
        cmd_buffer[cmd_len++] = input;
        cmd_buffer[cmd_len] = '\0';
    }
}

static void execute_command(char *cmd) {
    char *command = next_token(&cmd);
    if (!command) {
        return;
    }

    seL4_Bool err = 0;

    if (str_eq(command, "help")) {
        print_help();
    } else if (str_eq(command, "status")) {
        char *arg = next_token(&cmd);
        if (arg) {
            uint8_t core_id = str_to_int(arg);
            err = send_core_command(CORE_STATUS, core_id, 0);
        } else {
            uart_puts("Usage: status <core_id>\n");
        }
    } else if (str_eq(command, "dump")) {
        char *arg = next_token(&cmd);
        if (arg) {
            uint8_t core_id = str_to_int(arg);
            dump_core(core_id);
        } else {
            uart_puts("Usage: dump <core_id>\n");
        }
    } else if (str_eq(command, "migrate")) {
        char *pd_arg = next_token(&cmd);
        char *core_arg = next_token(&cmd);
        if (pd_arg && str_eq(pd_arg, "monitor") && core_arg) {
            uint8_t core_id = str_to_int(core_arg);
            err = send_core_command(CORE_MIGRATE_MONITOR, core_id, 0);
            if (!err) {
                monitor_core = core_id;
            }
        } else if (pd_arg && core_arg) {
            uint8_t pd_id = str_to_int(pd_arg);
            uint8_t core_id = str_to_int(core_arg);
            if (!err) {
                // Update local state
                for (int c = 0; c < NUM_CPUS; c++) {
                    if (core_pds[c][pd_id][0] != '\0') {
                        err = migrate_pd(c, core_id, pd_id);
                        break;
                    }
                }
            }
        } else {
            uart_puts("Usage: migrate <pd_id> <core> OR migrate monitor <core>\n");
        }
    } else if (str_eq(command, "off")) {
        char *arg = next_token(&cmd);
        if (arg) {
            uint8_t core_id = str_to_int(arg);
            err = send_core_command(CORE_OFF, core_id, 0);
        } else {
            uart_puts("Usage: off <core_id>\n");
        }
    } else if (str_eq(command, "powerdown")) {
        char *arg = next_token(&cmd);
        if (arg) {
            uint8_t core_id = str_to_int(arg);
            err = send_core_command(CORE_POWERDOWN, core_id, 0);
        } else {
            uart_puts("Usage: powerdown <core_id>\n");
        }
    } else if (str_eq(command, "standby")) {
        char *arg = next_token(&cmd);
        if (arg) {
            uint8_t core_id = str_to_int(arg);
            err = send_core_command(CORE_STANDBY, core_id, 0);
        } else {
            uart_puts("Usage: standby <core_id>\n");
        }
    } else if (str_eq(command, "on")) {
        char *arg = next_token(&cmd);
        if (arg) {
            uint8_t core_id = str_to_int(arg);
            err = send_core_command(CORE_ON, core_id, 0);
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

static seL4_Bool send_core_command(Instruction cmd, uint8_t core_id, uint8_t pd_id) {
    microkit_mr_set(0, cmd);
    microkit_mr_set(1, core_id);
    microkit_mr_set(2, pd_id);

    microkit_ppcall(API_CHANNEL, microkit_msginfo_new(0, 3));
    
    return microkit_mr_get(0);
}

static void dump_core(uint8_t core) {
    if (core >= NUM_CPUS) {
        uart_puts("Invalid core ID.\n");
        return;
    }

    uart_puts("=== Dumping Protection Domains for Core ");
    uart_put64(core);
    uart_puts(" ===\n");
    uart_puts("PD ID\tName\n");
    uart_puts("----------------------\n");

    if (core == monitor_core) {
        uart_puts("\tMicrokit Monitor\n");
    }

    for (int pd_id = 0; pd_id < MAX_PDS; pd_id++) {
        char *name = core_pds[core][pd_id];
        if (name[0] != '\0') {
            uart_put64(pd_id);
            uart_puts("\t");
            uart_puts(name);
            uart_putc('\n');
        }
    }

    uart_puts("=== End Dump ===\n");
}

static void print_help(void) {
    uart_puts(
        "\n=== CORE MANAGEMENT COMMANDS ===\n"
        "help                     : Show this help message\n"
        "status <core_id>         : View the status of a core\n"
        "dump <core_id>           : Dump the protection domains on a core\n"
        "migrate <pd_id> <core>   : Migrate a protection domain to a core\n"
        "migrate monitor <core>   : Migrate the monitor to a specific core\n"
        "off <core_id>            : Turn off a core\n"
        "powerdown <core_id>      : Power down a core\n"
        "standby <core_id>        : Put a core in standby mode\n"
        "on <core_id>             : Turn on a core\n"
        "\n"
    );
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

static char *skip_ws(char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    return p;
}

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

static int str_to_int(const char *s) {
    int val = 0;
    int is_negative = 0;
    
    if (*s == '-') {
        is_negative = 1;
        s++;
    }
    
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    
    return is_negative ? -val : val;
}

static void *memcpy(void *dst, const void *src, uint64_t sz) {
    char *dst_ = dst;
    const char *src_ = src;
    while (sz-- > 0) {
        *dst_++ = *src_++;
    }

    return dst;
}

static inline seL4_Bool migrate_pd(uint8_t from_core, uint8_t to_core, uint8_t pd_id) {
    memcpy(core_pds[to_core][pd_id], core_pds[from_core][pd_id], MICROKIT_PD_NAME_LENGTH);
    core_pds[from_core][pd_id][0] = '\0'; // mark old slot empty
    return send_core_command(CORE_MIGRATE, to_core, pd_id);
}
