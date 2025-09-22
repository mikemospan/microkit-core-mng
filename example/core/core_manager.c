#include "core.h"
#include "uart.h"

#define API_CHANNEL 1
#define BACKSPACE 127

// Command buffer for user input
char *cmd_buffer;
int cmd_len = 0;

// Function prototypes
static int str_eq(const char *a, const char *b);
static char *skip_ws(char *p);
static char *next_token(char **p);
static int str_to_int(const char *s);
static void execute_command(char *cmd);
static void handle_user_input(char input);
static void send_core_command(Instruction cmd, uint8_t core_id, uint8_t pd_id);
static void print_help(void);

void init(void) {
    uart_init();
    cmd_buffer[0] = '\0';
    cmd_len = 0;
}

void notified(microkit_channel ch) {
    if (ch != UART_IRQ_CH) {
        microkit_dbg_puts("[Core Manager]: Received unexpected notification: ");
        microkit_dbg_put32(ch);
        microkit_dbg_putc('\n');
        return;
    }

    char input = uart_get_char();
    uart_handle_irq();
    uart_put_char(input);
    
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
            uart_put_str("\b \b"); // Erase character on terminal
        }
    } else {
        // Add character to buffer
        cmd_buffer[cmd_len++] = input;
        cmd_buffer[cmd_len] = '\0';
    }
}

static void execute_command(char *cmd) {
    char *p = cmd;
    char *command = next_token(&p);
    if (!command) {
        return;
    }

    if (str_eq(command, "help")) {
        print_help();
    } else if (str_eq(command, "status")) {
        char *arg = next_token(&p);
        if (arg) {
            uint8_t core_id = str_to_int(arg);
            send_core_command(CORE_STATUS, core_id, 0);
        } else {
            microkit_dbg_puts("Usage: status <core_id>\n");
        }
    } else if (str_eq(command, "dump")) {
        char *arg = next_token(&p);
        if (arg) {
            uint8_t core_id = str_to_int(arg);
            send_core_command(CORE_DUMP, core_id, 0);
        } else {
            microkit_dbg_puts("Usage: dump <core_id>\n");
        }
    } else if (str_eq(command, "migrate")) {
        char *pd_arg = next_token(&p);
        char *core_arg = next_token(&p);
        if (pd_arg && core_arg) {
            uint8_t pd_id = str_to_int(pd_arg);
            uint8_t core_id = str_to_int(core_arg);
            send_core_command(CORE_MIGRATE, core_id, pd_id);
        } else {
            microkit_dbg_puts("Usage: migrate <pd_id> <core_id>\n");
        }
    } else if (str_eq(command, "off")) {
        char *arg = next_token(&p);
        if (arg) {
            uint8_t core_id = str_to_int(arg);
            send_core_command(CORE_OFF, core_id, 0);
        } else {
            microkit_dbg_puts("Usage: off <core_id>\n");
        }
    } else if (str_eq(command, "powerdown")) {
        char *arg = next_token(&p);
        if (arg) {
            uint8_t core_id = str_to_int(arg);
            send_core_command(CORE_POWERDOWN, core_id, 0);
        } else {
            microkit_dbg_puts("Usage: powerdown <core_id>\n");
        }
    } else if (str_eq(command, "standby")) {
        char *arg = next_token(&p);
        if (arg) {
            uint8_t core_id = str_to_int(arg);
            send_core_command(CORE_STANDBY, core_id, 0);
        } else {
            microkit_dbg_puts("Usage: standby <core_id>\n");
        }
    } else if (str_eq(command, "on")) {
        char *arg = next_token(&p);
        if (arg) {
            uint8_t core_id = str_to_int(arg);
            send_core_command(CORE_ON, core_id, 0);
        } else {
            microkit_dbg_puts("Usage: on <core_id>\n");
        }
    } else {
        microkit_dbg_puts("Unknown command. Type 'help' for a list of commands.\n");
    }
}

static void send_core_command(Instruction cmd, uint8_t core_id, uint8_t pd_id) {
    microkit_mr_set(0, cmd);
    microkit_mr_set(1, core_id);
    microkit_mr_set(2, pd_id);

    microkit_ppcall(API_CHANNEL, microkit_msginfo_new(0, 3));
    
    seL4_Bool failed = microkit_mr_get(0);
    if (failed) {
        microkit_dbg_puts("Core Manager API request failed.\n");
    }
}

static void print_help(void) {
    microkit_dbg_puts(
        "\n=== CORE MANAGEMENT COMMANDS ===\n"
        "help                     : Show this help message\n"
        "status <core_id>         : View the status of a core\n"
        "dump <core_id>           : Dump the protection domains on a core\n"
        "migrate <pd_id> <core>   : Migrate a protection domain to a core\n"
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
