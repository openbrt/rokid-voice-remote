/*
 * rokid-voice-remote: minimal Broadcom BSA HID Device controller.
 *
 * Vendor headers and libraries are intentionally not included in this repo.
 * Build against the matching, user-owned Rokid SDK and dynamically link the
 * resulting binary to the factory /usr/lib/libbsa.so.
 */

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <bsa_api.h>

#define DEFAULT_SOCKET "/run/rokid-voice-remote/hidd.sock"
#define DEFAULT_UIPC "/data/bluetooth/"
#define DEFAULT_NAME "Rokid Voice Remote"
#define COMMAND_MAX 512
#define RESPONSE_MAX 512
#define CONNECT_TIMEOUT_SECONDS 8
#define MAX_REPEAT 10

static volatile sig_atomic_t g_stop;
static pthread_mutex_t g_state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_state_cond = PTHREAD_COND_INITIALIZER;
static bool g_mgt_connected;
static bool g_hd_enabled;
static bool g_hd_connected;
static BD_ADDR g_connected_addr;
static BD_ADDR g_enabled_addr;

static void log_line(const char *level, const char *format, ...)
{
    va_list args;

    fprintf(stderr, "voice_remote_hid[%s] ", level);
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    fflush(stderr);
}

static void format_addr(const BD_ADDR addr, char output[18])
{
    snprintf(output, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

static bool addr_equal(const BD_ADDR left, const BD_ADDR right)
{
    return memcmp(left, right, sizeof(BD_ADDR)) == 0;
}

static bool parse_addr(const char *text_value, BD_ADDR output)
{
    unsigned int octets[6];
    char trailing;
    int result;
    size_t index;

    result = sscanf(text_value, "%2x:%2x:%2x:%2x:%2x:%2x%c",
                    &octets[0], &octets[1], &octets[2], &octets[3],
                    &octets[4], &octets[5], &trailing);
    if (result != 6) {
        return false;
    }
    for (index = 0; index < 6; ++index) {
        if (octets[index] > 0xffU) {
            return false;
        }
        output[index] = (UINT8)octets[index];
    }
    return true;
}

static bool parse_unsigned(const char *text_value, unsigned long maximum,
                           unsigned long *output)
{
    char *end = NULL;
    unsigned long value;

    if (text_value == NULL || text_value[0] == '\0' || text_value[0] == '-') {
        return false;
    }
    errno = 0;
    value = strtoul(text_value, &end, 0);
    if (errno != 0 || end == text_value || *end != '\0' || value > maximum) {
        return false;
    }
    *output = value;
    return true;
}

static void sleep_milliseconds(long milliseconds)
{
    struct timespec duration;

    duration.tv_sec = milliseconds / 1000;
    duration.tv_nsec = (milliseconds % 1000) * 1000000L;
    while (nanosleep(&duration, &duration) != 0 && errno == EINTR) {
    }
}

static void signal_handler(int signal_number)
{
    (void)signal_number;
    g_stop = 1;
}

static void management_callback(tBSA_MGT_EVT event, tBSA_MGT_MSG *message)
{
    pthread_mutex_lock(&g_state_lock);
    if (event == BSA_MGT_DISCONNECT_EVT) {
        g_mgt_connected = false;
        g_hd_connected = false;
        g_stop = 1;
        log_line("error", "BSA management disconnected, reason=%d",
                 message != NULL ? message->disconnect.reason : -1);
    } else if (event == BSA_MGT_STATUS_EVT && message != NULL) {
        g_mgt_connected = message->status.enable ? true : false;
        if (!g_mgt_connected) {
            g_hd_connected = false;
        }
        log_line("info", "Bluetooth status enable=%d", message->status.enable);
    }
    pthread_cond_broadcast(&g_state_cond);
    pthread_mutex_unlock(&g_state_lock);
}

static void hd_callback(tBSA_HD_EVT event, tBSA_HD_MSG *message)
{
    char address[18] = "unknown";

    pthread_mutex_lock(&g_state_lock);
    switch (event) {
    case BSA_HD_OPEN_EVT:
        if (message != NULL && message->open.status == BSA_SUCCESS) {
            memcpy(g_connected_addr, message->open.bd_addr, sizeof(BD_ADDR));
            g_hd_connected = true;
            format_addr(g_connected_addr, address);
            log_line("info", "HID host connected address=%s", address);
        } else {
            g_hd_connected = false;
            log_line("error", "HID open failed status=%d",
                     message != NULL ? message->open.status : -1);
        }
        break;
    case BSA_HD_CLOSE_EVT:
        if (message != NULL) {
            format_addr(message->close.bd_addr, address);
        }
        g_hd_connected = false;
        memset(g_connected_addr, 0, sizeof(BD_ADDR));
        log_line("info", "HID host disconnected address=%s status=%d", address,
                 message != NULL ? message->close.status : -1);
        break;
    case BSA_HD_UNPLUG_EVT:
        g_hd_connected = false;
        memset(g_connected_addr, 0, sizeof(BD_ADDR));
        log_line("info", "HID host requested virtual-cable unplug");
        break;
    case BSA_HD_DATA_EVT:
    case BSA_HD_DATC_EVT:
        break;
    default:
        log_line("error", "unknown HID event=%d", event);
        break;
    }
    pthread_cond_broadcast(&g_state_cond);
    pthread_mutex_unlock(&g_state_lock);
}

static int open_management(const char *uipc_path)
{
    tBSA_MGT_OPEN parameters;
    tBSA_STATUS status = BSA_ERROR_CLI_NOT_CONNECTED;
    int attempt;

    if (strlen(uipc_path) >= sizeof(parameters.uipc_path)) {
        log_line("error", "UIPC path is too long");
        return -1;
    }

    BSA_MgtOpenInit(&parameters);
    strncpy(parameters.uipc_path, uipc_path, sizeof(parameters.uipc_path) - 1);
    parameters.uipc_path[sizeof(parameters.uipc_path) - 1] = '\0';
    parameters.callback = management_callback;

    for (attempt = 1; attempt <= 10 && !g_stop; ++attempt) {
        status = BSA_MgtOpen(&parameters);
        if (status == BSA_SUCCESS) {
            pthread_mutex_lock(&g_state_lock);
            g_mgt_connected = true;
            pthread_mutex_unlock(&g_state_lock);
            return 0;
        }
        log_line("error", "BSA management open failed status=%d attempt=%d",
                 status, attempt);
        sleep(1);
    }
    return -1;
}

static void close_management(void)
{
    tBSA_MGT_CLOSE parameters;

    BSA_MgtCloseInit(&parameters);
    (void)BSA_MgtClose(&parameters);
    pthread_mutex_lock(&g_state_lock);
    g_mgt_connected = false;
    pthread_mutex_unlock(&g_state_lock);
}

static int set_local_config(const char *name)
{
    tBSA_DM_SET_CONFIG parameters;
    const DEV_CLASS hid_class = {0x00, 0x05, 0xC0};

    if (strlen(name) >= sizeof(parameters.name)) {
        log_line("error", "Bluetooth name is too long");
        return -1;
    }
    if (BSA_DmSetConfigInit(&parameters) != BSA_SUCCESS) {
        return -1;
    }

    parameters.config_mask = BSA_DM_CONFIG_VISIBILITY_MASK |
                             BSA_DM_CONFIG_NAME_MASK |
                             BSA_DM_CONFIG_DEV_CLASS_MASK;
    parameters.discoverable = TRUE;
    parameters.connectable = TRUE;
    strncpy((char *)parameters.name, name, sizeof(parameters.name) - 1);
    parameters.name[sizeof(parameters.name) - 1] = '\0';
    memcpy(parameters.class_of_device, hid_class, sizeof(DEV_CLASS));

    if (BSA_DmSetConfig(&parameters) != BSA_SUCCESS) {
        log_line("error", "BSA_DmSetConfig failed");
        return -1;
    }
    return 0;
}

static int disable_hid(void)
{
    tBSA_HD_DISABLE parameters;
    tBSA_STATUS status;

    if (!g_hd_enabled) {
        return 0;
    }
    BSA_HdDisableInit(&parameters);
    status = BSA_HdDisable(&parameters);
    if (status != BSA_SUCCESS) {
        log_line("error", "BSA_HdDisable failed status=%d", status);
        return -1;
    }
    pthread_mutex_lock(&g_state_lock);
    g_hd_enabled = false;
    g_hd_connected = false;
    memset(g_connected_addr, 0, sizeof(BD_ADDR));
    pthread_mutex_unlock(&g_state_lock);
    return 0;
}

static int enable_hid(const BD_ADDR address)
{
    tBSA_HD_ENABLE parameters;
    tBSA_STATUS status;

    BSA_HdEnableInit(&parameters);
    parameters.sec_mask = BSA_SEC_NONE;
    memcpy(parameters.bd_addr, address, sizeof(BD_ADDR));
    parameters.p_cback = hd_callback;
    status = BSA_HdEnable(&parameters);
    if (status != BSA_SUCCESS) {
        log_line("error", "BSA_HdEnable failed status=%d", status);
        return -1;
    }
    pthread_mutex_lock(&g_state_lock);
    memcpy(g_enabled_addr, address, sizeof(BD_ADDR));
    g_hd_enabled = true;
    pthread_mutex_unlock(&g_state_lock);
    return 0;
}

static int listen_for_host(void)
{
    BD_ADDR any_host = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    if (disable_hid() != 0) {
        return -1;
    }
    if (enable_hid(any_host) != 0) {
        return -1;
    }
    log_line("info", "listening for a HID host");
    return 0;
}

static int wait_for_connection(const BD_ADDR expected)
{
    struct timespec deadline;
    int result = -1;

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += CONNECT_TIMEOUT_SECONDS;

    pthread_mutex_lock(&g_state_lock);
    while (!g_stop) {
        if (g_hd_connected && addr_equal(g_connected_addr, expected)) {
            result = 0;
            break;
        }
        if (pthread_cond_timedwait(&g_state_cond, &g_state_lock, &deadline) ==
            ETIMEDOUT) {
            break;
        }
    }
    pthread_mutex_unlock(&g_state_lock);
    return result;
}

static int connect_target(const BD_ADDR address)
{
    tBSA_HD_OPEN open_parameters;
    tBSA_STATUS status;

    pthread_mutex_lock(&g_state_lock);
    if (g_hd_connected && addr_equal(g_connected_addr, address)) {
        pthread_mutex_unlock(&g_state_lock);
        return 0;
    }
    pthread_mutex_unlock(&g_state_lock);

    if (disable_hid() != 0 || enable_hid(address) != 0) {
        return -1;
    }
    BSA_HdOpenInit(&open_parameters);
    open_parameters.sec_mask = BSA_SEC_NONE;
    status = BSA_HdOpen(&open_parameters);
    if (status != BSA_SUCCESS) {
        log_line("error", "BSA_HdOpen failed status=%d", status);
        return -1;
    }
    return wait_for_connection(address);
}

static bool is_connected(void)
{
    bool connected;

    pthread_mutex_lock(&g_state_lock);
    connected = g_hd_connected;
    pthread_mutex_unlock(&g_state_lock);
    return connected;
}

static int send_consumer_once(uint16_t usage)
{
    tBSA_HD_SEND parameters;
    tBSA_STATUS status;

    BSA_HdSendInit(&parameters);
    parameters.key_type = BSA_HD_CUSTOMER_DATA;
    parameters.param.customer.data_len = 5;
    parameters.param.customer.data[0] = 0x03;
    parameters.param.customer.data[1] = (UINT8)(usage & 0xffU);
    parameters.param.customer.data[2] = (UINT8)((usage >> 8) & 0xffU);
    parameters.param.customer.data[3] = 0x00;
    parameters.param.customer.data[4] = 0x00;
    status = BSA_HdSend(&parameters);
    if (status != BSA_SUCCESS) {
        return -1;
    }

    sleep_milliseconds(35);
    parameters.param.customer.data[1] = 0x00;
    parameters.param.customer.data[2] = 0x00;
    status = BSA_HdSend(&parameters);
    return status == BSA_SUCCESS ? 0 : -1;
}

static int send_consumer(uint16_t usage, unsigned int repeat)
{
    unsigned int index;

    if (!is_connected()) {
        return -1;
    }
    for (index = 0; index < repeat; ++index) {
        if (send_consumer_once(usage) != 0) {
            return -1;
        }
        if (index + 1 < repeat) {
            sleep_milliseconds(65);
        }
    }
    return 0;
}

static int send_key(uint8_t key_code, uint8_t modifier, unsigned int repeat)
{
    tBSA_HD_SEND parameters;
    unsigned int index;

    if (!is_connected()) {
        return -1;
    }
    BSA_HdSendInit(&parameters);
    parameters.key_type = BSA_HD_REGULAR_KEY;
    parameters.param.reg_key.auto_release = TRUE;
    parameters.param.reg_key.key_code = key_code;
    parameters.param.reg_key.modifier = modifier;

    for (index = 0; index < repeat; ++index) {
        if (BSA_HdSend(&parameters) != BSA_SUCCESS) {
            return -1;
        }
        if (index + 1 < repeat) {
            sleep_milliseconds(100);
        }
    }
    return 0;
}

static int response(char *output, size_t output_size, bool ok,
                    const char *format, ...)
{
    va_list args;
    int prefix;

    prefix = snprintf(output, output_size, "%s ", ok ? "OK" : "ERR");
    if (prefix < 0 || (size_t)prefix >= output_size) {
        return -1;
    }
    va_start(args, format);
    vsnprintf(output + prefix, output_size - (size_t)prefix, format, args);
    va_end(args);
    return ok ? 0 : -1;
}

static int write_all(int descriptor, const char *data, size_t length)
{
    size_t offset = 0;

    while (offset < length) {
        ssize_t written = write(descriptor, data + offset, length - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (written == 0) {
            return -1;
        }
        offset += (size_t)written;
    }
    return 0;
}

static int handle_command(char *command, char *output, size_t output_size)
{
    char *save = NULL;
    char *verb;
    char *argument1;
    char *argument2;
    char *argument3;
    unsigned long value1;
    unsigned long value2 = 0;
    unsigned long repeat = 1;
    BD_ADDR address;
    char formatted[18];
    bool connected;
    bool management_connected;
    bool enabled;

    command[strcspn(command, "\r\n")] = '\0';
    verb = strtok_r(command, " \t", &save);
    if (verb == NULL) {
        return response(output, output_size, false, "empty command");
    }

    if (strcmp(verb, "status") == 0) {
        pthread_mutex_lock(&g_state_lock);
        connected = g_hd_connected;
        if (connected) {
            format_addr(g_connected_addr, formatted);
        } else {
            strcpy(formatted, "none");
        }
        management_connected = g_mgt_connected;
        enabled = g_hd_enabled;
        pthread_mutex_unlock(&g_state_lock);
        return response(output, output_size, true,
                        "management=%d enabled=%d connected=%d address=%s",
                        management_connected ? 1 : 0, enabled ? 1 : 0,
                        connected ? 1 : 0, formatted);
    }

    if (strcmp(verb, "listen") == 0) {
        return response(output, output_size, listen_for_host() == 0,
                        "listen");
    }

    if (strcmp(verb, "target") == 0) {
        argument1 = strtok_r(NULL, " \t", &save);
        if (argument1 == NULL || strtok_r(NULL, " \t", &save) != NULL ||
            !parse_addr(argument1, address)) {
            return response(output, output_size, false,
                            "usage: target XX:XX:XX:XX:XX:XX");
        }
        format_addr(address, formatted);
        if (connect_target(address) != 0) {
            return response(output, output_size, false,
                            "target connection failed address=%s", formatted);
        }
        return response(output, output_size, true, "target address=%s",
                        formatted);
    }

    if (strcmp(verb, "consumer") == 0) {
        argument1 = strtok_r(NULL, " \t", &save);
        argument2 = strtok_r(NULL, " \t", &save);
        argument3 = strtok_r(NULL, " \t", &save);
        if (!parse_unsigned(argument1, 0x028cUL, &value1) ||
            (argument2 != NULL &&
             !parse_unsigned(argument2, MAX_REPEAT, &repeat)) ||
            repeat < 1 || argument3 != NULL) {
            return response(output, output_size, false,
                            "usage: consumer USAGE [1-%d]", MAX_REPEAT);
        }
        if (send_consumer((uint16_t)value1, (unsigned int)repeat) != 0) {
            return response(output, output_size, false,
                            "consumer send failed (host not connected?)");
        }
        return response(output, output_size, true,
                        "consumer usage=0x%04lx repeat=%lu", value1, repeat);
    }

    if (strcmp(verb, "key") == 0) {
        argument1 = strtok_r(NULL, " \t", &save);
        argument2 = strtok_r(NULL, " \t", &save);
        argument3 = strtok_r(NULL, " \t", &save);
        if (!parse_unsigned(argument1, 0xffUL, &value1) ||
            (argument2 != NULL && !parse_unsigned(argument2, 0xffUL, &value2)) ||
            (argument3 != NULL &&
             !parse_unsigned(argument3, MAX_REPEAT, &repeat)) ||
            repeat < 1 || strtok_r(NULL, " \t", &save) != NULL) {
            return response(output, output_size, false,
                            "usage: key KEY_CODE [MODIFIER] [1-%d]", MAX_REPEAT);
        }
        if (send_key((uint8_t)value1, (uint8_t)value2,
                     (unsigned int)repeat) != 0) {
            return response(output, output_size, false,
                            "key send failed (host not connected?)");
        }
        return response(output, output_size, true,
                        "key code=0x%02lx modifier=0x%02lx repeat=%lu",
                        value1, value2, repeat);
    }

    return response(output, output_size, false, "unknown command");
}

static int make_server_socket(const char *socket_path)
{
    int descriptor;
    struct sockaddr_un address;
    struct stat existing;

    if (strlen(socket_path) >= sizeof(address.sun_path)) {
        log_line("error", "socket path is too long");
        return -1;
    }
    if (lstat(socket_path, &existing) == 0) {
        if (!S_ISSOCK(existing.st_mode)) {
            log_line("error", "refusing to replace non-socket path=%s",
                     socket_path);
            return -1;
        }
        if (unlink(socket_path) != 0) {
            log_line("error", "cannot remove stale socket: %s", strerror(errno));
            return -1;
        }
    } else if (errno != ENOENT) {
        log_line("error", "cannot inspect socket path: %s", strerror(errno));
        return -1;
    }

    descriptor = socket(AF_UNIX, SOCK_STREAM, 0);
    if (descriptor < 0) {
        return -1;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, socket_path, sizeof(address.sun_path) - 1);
    umask(007);
    if (bind(descriptor, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(descriptor, 8) != 0) {
        log_line("error", "cannot bind/listen socket: %s", strerror(errno));
        close(descriptor);
        return -1;
    }
    (void)chmod(socket_path, 0660);
    return descriptor;
}

static int run_server(const char *socket_path)
{
    int server;
    int client;
    fd_set read_set;
    struct timeval timeout;
    char command[COMMAND_MAX];
    char reply[RESPONSE_MAX];
    ssize_t count;

    server = make_server_socket(socket_path);
    if (server < 0) {
        return -1;
    }
    log_line("info", "control socket ready path=%s", socket_path);

    while (!g_stop) {
        FD_ZERO(&read_set);
        FD_SET(server, &read_set);
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        if (select(server + 1, &read_set, NULL, NULL, &timeout) < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (!FD_ISSET(server, &read_set)) {
            continue;
        }
        client = accept(server, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        count = read(client, command, sizeof(command) - 1);
        if (count > 0) {
            command[count] = '\0';
            (void)handle_command(command, reply, sizeof(reply));
            strncat(reply, "\n", sizeof(reply) - strlen(reply) - 1);
            if (write_all(client, reply, strlen(reply)) != 0) {
                log_line("error", "failed to write control response");
            }
        }
        close(client);
    }

    close(server);
    (void)unlink(socket_path);
    return g_stop ? 0 : -1;
}

static int run_daemon(int argc, char **argv)
{
    const char *socket_path = DEFAULT_SOCKET;
    const char *uipc_path = DEFAULT_UIPC;
    const char *name = DEFAULT_NAME;
    struct sigaction action;
    int index;
    int result;

    for (index = 2; index < argc; ++index) {
        if (strcmp(argv[index], "--socket") == 0 && index + 1 < argc) {
            socket_path = argv[++index];
        } else if (strcmp(argv[index], "--uipc") == 0 && index + 1 < argc) {
            uipc_path = argv[++index];
        } else if (strcmp(argv[index], "--name") == 0 && index + 1 < argc) {
            name = argv[++index];
        } else {
            fprintf(stderr, "unknown daemon option: %s\n", argv[index]);
            return 2;
        }
    }

    memset(&action, 0, sizeof(action));
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    signal(SIGPIPE, SIG_IGN);

    if (open_management(uipc_path) != 0 || set_local_config(name) != 0 ||
        listen_for_host() != 0) {
        close_management();
        return 1;
    }
    result = run_server(socket_path);
    (void)disable_hid();
    close_management();
    return result == 0 ? 0 : 1;
}

static int run_client(int argc, char **argv)
{
    const char *socket_path = DEFAULT_SOCKET;
    struct sockaddr_un address;
    char command[COMMAND_MAX] = {0};
    char reply[RESPONSE_MAX];
    int descriptor;
    int index = 2;
    size_t used = 0;
    ssize_t count;

    if (index < argc && strcmp(argv[index], "--socket") == 0 && index + 1 < argc) {
        socket_path = argv[index + 1];
        index += 2;
    }
    if (index >= argc || strlen(socket_path) >= sizeof(address.sun_path)) {
        fprintf(stderr, "ctl requires a command\n");
        return 2;
    }
    for (; index < argc; ++index) {
        int written = snprintf(command + used, sizeof(command) - used, "%s%s",
                               used == 0 ? "" : " ", argv[index]);
        if (written < 0 || (size_t)written >= sizeof(command) - used) {
            fprintf(stderr, "command is too long\n");
            return 2;
        }
        used += (size_t)written;
    }

    descriptor = socket(AF_UNIX, SOCK_STREAM, 0);
    if (descriptor < 0) {
        perror("socket");
        return 1;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, socket_path, sizeof(address.sun_path) - 1);
    if (connect(descriptor, (struct sockaddr *)&address, sizeof(address)) != 0) {
        perror("connect");
        close(descriptor);
        return 1;
    }
    if (write_all(descriptor, command, strlen(command)) != 0) {
        perror("write");
        close(descriptor);
        return 1;
    }
    count = read(descriptor, reply, sizeof(reply) - 1);
    close(descriptor);
    if (count <= 0) {
        fprintf(stderr, "empty response\n");
        return 1;
    }
    reply[count] = '\0';
    fputs(reply, stdout);
    return strncmp(reply, "OK ", 3) == 0 ? 0 : 1;
}

static void print_usage(const char *program)
{
    fprintf(stderr,
            "usage:\n"
            "  %s daemon [--socket PATH] [--uipc PATH] [--name NAME]\n"
            "  %s ctl [--socket PATH] status|listen\n"
            "  %s ctl [--socket PATH] target XX:XX:XX:XX:XX:XX\n"
            "  %s ctl [--socket PATH] consumer USAGE [REPEAT]\n"
            "  %s ctl [--socket PATH] key CODE [MODIFIER] [REPEAT]\n",
            program, program, program, program, program);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "daemon") == 0) {
        return run_daemon(argc, argv);
    }
    if (strcmp(argv[1], "ctl") == 0) {
        return run_client(argc, argv);
    }
    print_usage(argv[0]);
    return 2;
}
