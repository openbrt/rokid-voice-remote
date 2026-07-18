/*
 * Minimal authenticated configuration server for rokid-voice-remote.
 * Only fixed static assets and a small, validated form API are exposed.
 */

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define DEFAULT_ROOT "/data/rokid-voice-remote"
#define DEFAULT_BIND "0.0.0.0"
#define DEFAULT_PORT 8090
#define MAX_REQUEST (128U * 1024U)
#define MAX_HEADER (16U * 1024U)
#define MAX_CONFIG (64U * 1024U)
#define MAX_STATIC (512U * 1024U)
#define MAX_COMMANDS 5
#define MAX_TARGETS 32
#define HID_SOCKET "/run/rokid-voice-remote/hidd.sock"

typedef struct {
    char method[8];
    char path[160];
    size_t content_length;
    char *body;
} http_request;

typedef struct {
    char names[MAX_TARGETS][33];
    size_t count;
} target_names;

static volatile sig_atomic_t g_stop;

static void stop_handler(int signal_number)
{
    (void)signal_number;
    g_stop = 1;
}

static int write_all(int descriptor, const void *data, size_t length)
{
    const char *bytes = data;
    size_t offset = 0;

    while (offset < length) {
        ssize_t written = write(descriptor, bytes + offset, length - offset);
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

static int send_response(int client, int status, const char *reason,
                         const char *content_type, const char *body,
                         size_t body_length, bool no_store)
{
    char header[1024];
    int length;

    length = snprintf(
        header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "X-Frame-Options: DENY\r\n"
        "Referrer-Policy: no-referrer\r\n"
        "Content-Security-Policy: default-src 'self'; script-src 'self'; "
        "style-src 'self'; img-src 'self' data:; connect-src 'self'; "
        "frame-ancestors 'none'; base-uri 'none'; form-action 'self'\r\n"
        "%s"
        "\r\n",
        status, reason, content_type, body_length,
        no_store ? "Cache-Control: no-store\r\n" : "Cache-Control: no-cache\r\n");
    if (length < 0 || (size_t)length >= sizeof(header)) {
        return -1;
    }
    if (write_all(client, header, (size_t)length) != 0) {
        return -1;
    }
    return body_length == 0 || write_all(client, body, body_length) == 0 ? 0 : -1;
}

static int send_text(int client, int status, const char *reason,
                     const char *message)
{
    return send_response(client, status, reason, "text/plain; charset=utf-8",
                         message, strlen(message), true);
}

static int enter_hid_pairing_mode(void)
{
    struct sockaddr_un address;
    char reply[256];
    int descriptor;
    ssize_t count;

    if (strlen(HID_SOCKET) >= sizeof(address.sun_path)) {
        return -1;
    }
    descriptor = socket(AF_UNIX, SOCK_STREAM, 0);
    if (descriptor < 0) {
        return -1;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, HID_SOCKET, sizeof(address.sun_path) - 1);
    if (connect(descriptor, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        write_all(descriptor, "listen", strlen("listen")) != 0) {
        close(descriptor);
        return -1;
    }
    count = read(descriptor, reply, sizeof(reply) - 1);
    close(descriptor);
    if (count <= 0) {
        return -1;
    }
    reply[count] = '\0';
    return strncmp(reply, "OK ", 3) == 0 ? 0 : -1;
}

static int read_file(const char *path, size_t maximum, char **output,
                     size_t *output_length)
{
    int descriptor;
    struct stat metadata;
    char *data;
    size_t used = 0;

    descriptor = open(path, O_RDONLY);
    if (descriptor < 0 || fstat(descriptor, &metadata) != 0 ||
        metadata.st_size < 0 || (uint64_t)metadata.st_size > maximum) {
        if (descriptor >= 0) {
            close(descriptor);
        }
        return -1;
    }
    data = malloc((size_t)metadata.st_size + 1);
    if (data == NULL) {
        close(descriptor);
        return -1;
    }
    while (used < (size_t)metadata.st_size) {
        ssize_t count = read(descriptor, data + used,
                             (size_t)metadata.st_size - used);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            free(data);
            close(descriptor);
            return -1;
        }
        if (count == 0) {
            break;
        }
        used += (size_t)count;
    }
    close(descriptor);
    data[used] = '\0';
    *output = data;
    *output_length = used;
    return 0;
}

static int send_file(int client, const char *path, const char *content_type,
                     bool no_store)
{
    char *data;
    size_t length;
    int result;

    if (read_file(path, MAX_STATIC, &data, &length) != 0) {
        return send_text(client, 404, "Not Found", "not found\n");
    }
    result = send_response(client, 200, "OK", content_type, data, length,
                           no_store);
    free(data);
    return result;
}

static char *find_header_end(char *buffer)
{
    return strstr(buffer, "\r\n\r\n");
}

static char *trim_header_value(char *value)
{
    char *end;

    while (*value == ' ' || *value == '\t') {
        ++value;
    }
    end = value + strlen(value);
    while (end > value && (end[-1] == ' ' || end[-1] == '\t')) {
        *--end = '\0';
    }
    return value;
}

static bool parse_size(const char *text_value, size_t maximum, size_t *output)
{
    char *end = NULL;
    unsigned long value;

    if (text_value == NULL || *text_value == '\0' || *text_value == '-') {
        return false;
    }
    errno = 0;
    value = strtoul(text_value, &end, 10);
    if (errno != 0 || end == text_value || *end != '\0' || value > maximum) {
        return false;
    }
    *output = (size_t)value;
    return true;
}

static int receive_request(int client, char **storage, http_request *request)
{
    char *buffer = calloc(1, MAX_REQUEST + 1);
    char *header_end = NULL;
    char *line;
    char *save = NULL;
    char version[16];
    size_t used = 0;
    size_t header_length;
    size_t total_length;
    bool content_length_seen = false;
    bool transfer_encoding_seen = false;

    if (buffer == NULL) {
        return -1;
    }
    while (header_end == NULL) {
        ssize_t count;
        if (used >= MAX_HEADER) {
            free(buffer);
            return -2;
        }
        count = read(client, buffer + used, MAX_REQUEST - used);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            free(buffer);
            return -1;
        }
        if (count == 0) {
            free(buffer);
            return -2;
        }
        used += (size_t)count;
        buffer[used] = '\0';
        header_end = find_header_end(buffer);
    }

    header_length = (size_t)(header_end - buffer) + 4;
    *header_end = '\0';
    memset(request, 0, sizeof(*request));
    line = strtok_r(buffer, "\r\n", &save);
    if (line == NULL || sscanf(line, "%7s %159s %15s", request->method,
                               request->path, version) != 3 ||
        strcmp(version, "HTTP/1.1") != 0) {
        free(buffer);
        return -2;
    }
    while ((line = strtok_r(NULL, "\r\n", &save)) != NULL) {
        char *separator = strchr(line, ':');
        char *value;
        if (separator == NULL) {
            free(buffer);
            return -2;
        }
        *separator = '\0';
        value = trim_header_value(separator + 1);
        if (strcasecmp(line, "Content-Length") == 0) {
            if (content_length_seen ||
                !parse_size(value, MAX_CONFIG * 2U + 1024U,
                            &request->content_length)) {
                free(buffer);
                return -2;
            }
            content_length_seen = true;
        } else if (strcasecmp(line, "Transfer-Encoding") == 0) {
            transfer_encoding_seen = true;
        }
    }
    if (transfer_encoding_seen ||
        (strcmp(request->method, "POST") == 0 && !content_length_seen)) {
        free(buffer);
        return -2;
    }

    total_length = header_length + request->content_length;
    if (total_length > MAX_REQUEST) {
        free(buffer);
        return -2;
    }
    while (used < total_length) {
        ssize_t count = read(client, buffer + used, total_length - used);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            free(buffer);
            return -1;
        }
        if (count == 0) {
            free(buffer);
            return -2;
        }
        used += (size_t)count;
    }
    request->body = buffer + header_length;
    request->body[request->content_length] = '\0';
    *storage = buffer;
    return 0;
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static int url_decode(const char *encoded, char **decoded)
{
    size_t input_length = strlen(encoded);
    char *output;
    size_t input = 0;
    size_t used = 0;

    if (input_length > MAX_CONFIG * 3U) {
        return -1;
    }
    output = malloc(input_length + 1);
    if (output == NULL) {
        return -1;
    }
    while (input < input_length) {
        unsigned char value;
        if (encoded[input] == '+') {
            value = ' ';
            ++input;
        } else if (encoded[input] == '%') {
            int high;
            int low;
            if (input + 2 >= input_length ||
                (high = hex_value(encoded[input + 1])) < 0 ||
                (low = hex_value(encoded[input + 2])) < 0) {
                free(output);
                return -1;
            }
            value = (unsigned char)((high << 4) | low);
            input += 3;
        } else {
            value = (unsigned char)encoded[input++];
        }
        if (value == 0 || value == '\r' ||
            (value < 0x20 && value != '\n' && value != '\t') || value == 0x7f) {
            free(output);
            return -1;
        }
        output[used++] = (char)value;
        if (used > MAX_CONFIG) {
            free(output);
            return -1;
        }
    }
    output[used] = '\0';
    *decoded = output;
    return 0;
}

static size_t split_fields(char *line, char **fields, size_t maximum)
{
    size_t count = 1;
    char *cursor;

    fields[0] = line;
    for (cursor = line; *cursor != '\0'; ++cursor) {
        if (*cursor == '\t') {
            *cursor = '\0';
            if (count >= maximum) {
                return maximum + 1;
            }
            fields[count++] = cursor + 1;
        }
    }
    return count;
}

static bool safe_name(const char *value)
{
    size_t length = strlen(value);
    size_t index;

    if (length == 0 || length > 32) {
        return false;
    }
    for (index = 0; index < length; ++index) {
        unsigned char character = (unsigned char)value[index];
        if (!(isalnum(character) || character == '_' || character == '-' ||
              character == '.')) {
            return false;
        }
    }
    return true;
}

static bool valid_pinyin(const char *value)
{
    size_t length = strlen(value);
    size_t index;

    if (length == 0 || length > 256) {
        return false;
    }
    for (index = 0; index < length; ++index) {
        if (!isalnum((unsigned char)value[index])) {
            return false;
        }
    }
    return true;
}

static bool valid_address(const char *value)
{
    unsigned int octets[6];
    char trailing;
    size_t index;

    if (strlen(value) != 17 || value[2] != ':' || value[5] != ':' ||
        value[8] != ':' || value[11] != ':' || value[14] != ':' ||
        sscanf(value, "%2x:%2x:%2x:%2x:%2x:%2x%c", &octets[0],
               &octets[1], &octets[2], &octets[3], &octets[4],
               &octets[5], &trailing) != 6) {
        return false;
    }
    for (index = 0; index < 17; ++index) {
        if (index == 2 || index == 5 || index == 8 || index == 11 || index == 14) {
            continue;
        }
        if (!isxdigit((unsigned char)value[index])) {
            return false;
        }
    }
    return true;
}

static bool parse_number(const char *value, unsigned long maximum,
                         unsigned long *output)
{
    char *end = NULL;
    unsigned long number;

    if (*value == '\0' || *value == '-') {
        return false;
    }
    errno = 0;
    number = strtoul(value, &end, 0);
    if (errno != 0 || end == value || *end != '\0' || number > maximum) {
        return false;
    }
    *output = number;
    return true;
}

static int validate_targets(const char *configuration, target_names *targets,
                            char *error, size_t error_size)
{
    char *copy = strdup(configuration);
    char *save = NULL;
    char *line;
    size_t line_number = 0;

    if (copy == NULL) {
        snprintf(error, error_size, "out of memory");
        return -1;
    }
    memset(targets, 0, sizeof(*targets));
    line = strtok_r(copy, "\n", &save);
    while (line != NULL) {
        char *fields[2];
        size_t field_count;
        size_t index;
        ++line_number;
        if (*line == '\0' || *line == '#') {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        field_count = split_fields(line, fields, 2);
        if (field_count != 2 || !safe_name(fields[0]) ||
            !valid_address(fields[1])) {
            snprintf(error, error_size, "targets line %zu is invalid", line_number);
            free(copy);
            return -1;
        }
        if (targets->count >= MAX_TARGETS) {
            snprintf(error, error_size, "target count exceeds %d", MAX_TARGETS);
            free(copy);
            return -1;
        }
        for (index = 0; index < targets->count; ++index) {
            if (strcmp(targets->names[index], fields[0]) == 0) {
                snprintf(error, error_size, "duplicate target: %s", fields[0]);
                free(copy);
                return -1;
            }
        }
        strcpy(targets->names[targets->count++], fields[0]);
        line = strtok_r(NULL, "\n", &save);
    }
    free(copy);
    return 0;
}

static bool target_exists(const target_names *targets, const char *name)
{
    size_t index;

    if (strcmp(name, "active") == 0) {
        return true;
    }
    for (index = 0; index < targets->count; ++index) {
        if (strcmp(targets->names[index], name) == 0) {
            return true;
        }
    }
    return false;
}

static int validate_commands(const char *configuration,
                             const target_names *targets, size_t *command_count,
                             char *error, size_t error_size)
{
    char *copy = strdup(configuration);
    char *phrases[MAX_COMMANDS];
    char *save = NULL;
    char *line;
    size_t count = 0;
    size_t line_number = 0;

    if (copy == NULL) {
        snprintf(error, error_size, "out of memory");
        return -1;
    }
    line = strtok_r(copy, "\n", &save);
    while (line != NULL) {
        char *fields[6];
        size_t field_count;
        size_t index;
        unsigned long code;
        unsigned long repeat;
        unsigned long maximum;

        ++line_number;
        if (*line == '\0' || *line == '#') {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        field_count = split_fields(line, fields, 6);
        if (field_count != 6 || strlen(fields[0]) == 0 ||
            strlen(fields[0]) > 192 || !valid_pinyin(fields[1]) ||
            !safe_name(fields[2]) || !target_exists(targets, fields[2]) ||
            (strcmp(fields[3], "consumer") != 0 &&
             strcmp(fields[3], "key") != 0)) {
            snprintf(error, error_size, "commands line %zu is invalid", line_number);
            free(copy);
            return -1;
        }
        maximum = strcmp(fields[3], "consumer") == 0 ? 0x028cUL : 0xffUL;
        if (!parse_number(fields[4], maximum, &code) ||
            !parse_number(fields[5], 10, &repeat) || repeat < 1) {
            snprintf(error, error_size, "commands line %zu has invalid key data",
                     line_number);
            free(copy);
            return -1;
        }
        if (count >= MAX_COMMANDS) {
            snprintf(error, error_size, "command count exceeds %d", MAX_COMMANDS);
            free(copy);
            return -1;
        }
        for (index = 0; index < count; ++index) {
            if (strcmp(phrases[index], fields[0]) == 0) {
                snprintf(error, error_size, "duplicate phrase: %s", fields[0]);
                free(copy);
                return -1;
            }
        }
        phrases[count++] = fields[0];
        line = strtok_r(NULL, "\n", &save);
    }
    if (count == 0) {
        snprintf(error, error_size, "at least one command is required");
        free(copy);
        return -1;
    }
    *command_count = count;
    free(copy);
    return 0;
}

static int write_temporary(const char *path, const char *content)
{
    int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    size_t length = strlen(content);
    int result = 0;

    if (descriptor < 0) {
        return -1;
    }
    if (write_all(descriptor, content, length) != 0 || fsync(descriptor) != 0) {
        result = -1;
    }
    if (close(descriptor) != 0) {
        result = -1;
    }
    if (result != 0) {
        unlink(path);
    }
    return result;
}

static int save_configuration(const char *root, const char *commands,
                              const char *targets)
{
    char command_path[PATH_MAX];
    char target_path[PATH_MAX];
    char command_temp[PATH_MAX];
    char target_temp[PATH_MAX];
    char command_backup[PATH_MAX];
    char target_backup[PATH_MAX];
    long process = (long)getpid();
    int result = -1;

    if (snprintf(command_path, sizeof(command_path), "%s/config/commands.tsv", root) >=
            (int)sizeof(command_path) ||
        snprintf(target_path, sizeof(target_path), "%s/config/targets.conf", root) >=
            (int)sizeof(target_path) ||
        snprintf(command_temp, sizeof(command_temp), "%s/config/.commands.%ld.tmp",
                 root, process) >= (int)sizeof(command_temp) ||
        snprintf(target_temp, sizeof(target_temp), "%s/config/.targets.%ld.tmp", root,
                 process) >= (int)sizeof(target_temp) ||
        snprintf(command_backup, sizeof(command_backup),
                 "%s/config/.commands.%ld.bak", root, process) >=
            (int)sizeof(command_backup) ||
        snprintf(target_backup, sizeof(target_backup), "%s/config/.targets.%ld.bak",
                 root, process) >= (int)sizeof(target_backup)) {
        return -1;
    }

    unlink(command_temp);
    unlink(target_temp);
    unlink(command_backup);
    unlink(target_backup);
    if (write_temporary(command_temp, commands) != 0 ||
        write_temporary(target_temp, targets) != 0 ||
        link(command_path, command_backup) != 0 ||
        link(target_path, target_backup) != 0) {
        goto cleanup;
    }
    if (rename(command_temp, command_path) != 0) {
        goto cleanup;
    }
    if (rename(target_temp, target_path) != 0) {
        (void)rename(command_backup, command_path);
        goto cleanup;
    }
    (void)chmod(command_path, 0644);
    (void)chmod(target_path, 0644);
    result = 0;

cleanup:
    unlink(command_temp);
    unlink(target_temp);
    unlink(command_backup);
    unlink(target_backup);
    return result;
}

static char *extract_xml_value(char *line, const char *tag)
{
    char opening[80];
    char closing[80];
    char *start;
    char *end;

    if (snprintf(opening, sizeof(opening), "<%s>", tag) >=
            (int)sizeof(opening) ||
        snprintf(closing, sizeof(closing), "</%s>", tag) >=
            (int)sizeof(closing)) {
        return NULL;
    }
    start = strstr(line, opening);
    if (start == NULL) {
        return NULL;
    }
    start += strlen(opening);
    end = strstr(start, closing);
    if (end == NULL) {
        return NULL;
    }
    *end = '\0';
    return start;
}

static int paired_devices(char **output, size_t *output_length)
{
    FILE *database = fopen("/data/bluetooth/bt_devices.xml", "r");
    char *result = calloc(1, MAX_CONFIG + 1);
    char line[1024];
    char address[32] = "";
    char name[512] = "";
    bool key_present = false;
    size_t used = 0;

    if (result == NULL) {
        if (database != NULL) fclose(database);
        return -1;
    }
    if (database == NULL) {
        *output = result;
        *output_length = 0;
        return 0;
    }
    if (ferror(database)) {
        free(result);
        fclose(database);
        return -1;
    }
    while (fgets(line, sizeof(line), database) != NULL) {
        char *value;
        if ((value = extract_xml_value(line, "bd_addr")) != NULL) {
            snprintf(address, sizeof(address), "%s", value);
        } else if ((value = extract_xml_value(line, "device_name")) != NULL) {
            size_t index;
            snprintf(name, sizeof(name), "%s", *value == '\0' ? "(unnamed)" : value);
            for (index = 0; name[index] != '\0'; ++index) {
                if (name[index] == '\t' || name[index] == '\r' || name[index] == '\n') {
                    name[index] = ' ';
                }
            }
        } else if ((value = extract_xml_value(line, "Link_key_present")) != NULL) {
            key_present = strcmp(value, "1") == 0;
        } else if (strstr(line, "</device>") != NULL) {
            if (key_present && valid_address(address)) {
                int count = snprintf(result + used, MAX_CONFIG + 1 - used,
                                     "%s\t%s\n", address,
                                     *name == '\0' ? "(unnamed)" : name);
                if (count < 0 || (size_t)count >= MAX_CONFIG + 1 - used) {
                    fclose(database);
                    free(result);
                    return -1;
                }
                used += (size_t)count;
            }
            address[0] = '\0';
            name[0] = '\0';
            key_present = false;
        }
    }
    fclose(database);
    *output = result;
    *output_length = used;
    return 0;
}

static void restart_voice_service(void)
{
    pid_t child = fork();

    if (child == 0) {
        execl("/bin/systemctl", "systemctl", "restart",
              "rokid-voice-remote-voice.service", (char *)NULL);
        execl("/usr/bin/systemctl", "systemctl", "restart",
              "rokid-voice-remote-voice.service", (char *)NULL);
        _exit(127);
    }
}

static int send_root_file(int client, const char *root, const char *relative,
                          const char *content_type)
{
    char path[PATH_MAX];

    if (snprintf(path, sizeof(path), "%s/web/%s", root, relative) >=
        (int)sizeof(path)) {
        return send_text(client, 500, "Internal Server Error", "path too long\n");
    }
    return send_file(client, path, content_type, false);
}

static int handle_save(int client, const char *root, http_request *request,
                       bool *restart_requested)
{
    char *separator;
    char *commands = NULL;
    char *targets = NULL;
    target_names target_list;
    size_t command_count = 0;
    char error[256];
    char response_body[256];
    int result;

    if (strncmp(request->body, "commands=", 9) != 0 ||
        (separator = strstr(request->body + 9, "&targets=")) == NULL) {
        return send_text(client, 400, "Bad Request", "invalid form body\n");
    }
    *separator = '\0';
    if (url_decode(request->body + 9, &commands) != 0 ||
        url_decode(separator + 9, &targets) != 0) {
        free(commands);
        free(targets);
        return send_text(client, 400, "Bad Request", "invalid form encoding\n");
    }
    if (validate_targets(targets, &target_list, error, sizeof(error)) != 0 ||
        validate_commands(commands, &target_list, &command_count, error,
                          sizeof(error)) != 0) {
        char message[320];
        snprintf(message, sizeof(message), "validation failed: %s\n", error);
        free(commands);
        free(targets);
        return send_text(client, 422, "Unprocessable Content", message);
    }
    result = save_configuration(root, commands, targets);
    free(commands);
    free(targets);
    if (result != 0) {
        return send_text(client, 500, "Internal Server Error",
                         "configuration save failed\n");
    }
    snprintf(response_body, sizeof(response_body),
             "OK saved commands=%zu targets=%zu; voice listener restarting\n",
             command_count, target_list.count);
    *restart_requested = true;
    return send_text(client, 200, "OK", response_body);
}

static int handle_client(int client, const char *root, bool no_restart)
{
    http_request request;
    char *storage = NULL;
    char path[PATH_MAX];
    bool restart_requested = false;
    int parse_result;
    int result;

    parse_result = receive_request(client, &storage, &request);
    if (parse_result != 0) {
        return send_text(client, 400, "Bad Request", "bad request\n");
    }
    if (strcmp(request.method, "GET") == 0 &&
        (strcmp(request.path, "/") == 0 ||
         strcmp(request.path, "/index.html") == 0)) {
        result = send_root_file(client, root, "index.html", "text/html; charset=utf-8");
    } else if (strcmp(request.method, "GET") == 0 &&
               strcmp(request.path, "/app.js") == 0) {
        result = send_root_file(client, root, "app.js", "text/javascript; charset=utf-8");
    } else if (strcmp(request.method, "GET") == 0 &&
               strcmp(request.path, "/style.css") == 0) {
        result = send_root_file(client, root, "style.css", "text/css; charset=utf-8");
    } else if (strncmp(request.path, "/api/", 5) != 0) {
        result = send_text(client, 404, "Not Found", "not found\n");
    } else if (strcmp(request.method, "GET") == 0 &&
               strcmp(request.path, "/api/commands") == 0) {
        snprintf(path, sizeof(path), "%s/config/commands.tsv", root);
        result = send_file(client, path, "text/tab-separated-values; charset=utf-8", true);
    } else if (strcmp(request.method, "GET") == 0 &&
               strcmp(request.path, "/api/targets") == 0) {
        snprintf(path, sizeof(path), "%s/config/targets.conf", root);
        result = send_file(client, path, "text/tab-separated-values; charset=utf-8", true);
    } else if (strcmp(request.method, "GET") == 0 &&
               strcmp(request.path, "/api/paired") == 0) {
        char *paired;
        size_t paired_length;
        if (paired_devices(&paired, &paired_length) != 0) {
            result = send_text(client, 500, "Internal Server Error",
                               "cannot read paired devices\n");
        } else {
            result = send_response(client, 200, "OK",
                                   "text/tab-separated-values; charset=utf-8",
                                   paired, paired_length, true);
            free(paired);
        }
    } else if (strcmp(request.method, "GET") == 0 &&
               strcmp(request.path, "/api/status") == 0) {
        result = send_text(client, 200, "OK", "OK configuration service ready\n");
    } else if (strcmp(request.method, "POST") == 0 &&
               strcmp(request.path, "/api/hid/listen") == 0) {
        if (enter_hid_pairing_mode() != 0) {
            result = send_text(client, 503, "Service Unavailable",
                               "cannot enter Bluetooth pairing mode\n");
        } else {
            result = send_text(client, 200, "OK",
                               "OK Bluetooth pairing mode enabled\n");
        }
    } else if (strcmp(request.method, "POST") == 0 &&
               strcmp(request.path, "/api/config") == 0) {
        result = handle_save(client, root, &request, &restart_requested);
    } else {
        result = send_text(client, 405, "Method Not Allowed", "method not allowed\n");
    }
    free(storage);
    if (restart_requested && !no_restart) {
        restart_voice_service();
    }
    return result;
}

static int create_server(const char *bind_address, unsigned int port)
{
    int descriptor;
    int enabled = 1;
    struct sockaddr_in address;

    descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (descriptor < 0) {
        return -1;
    }
    (void)setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, bind_address, &address.sin_addr) != 1 ||
        bind(descriptor, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(descriptor, 8) != 0) {
        close(descriptor);
        return -1;
    }
    return descriptor;
}

static int serve(const char *root, const char *bind_address, unsigned int port,
                 bool no_restart)
{
    int server = create_server(bind_address, port);
    struct sigaction action;

    if (server < 0) {
        fprintf(stderr, "config server bind failed: %s\n", strerror(errno));
        return 1;
    }
    memset(&action, 0, sizeof(action));
    action.sa_handler = stop_handler;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    fprintf(stderr, "voice_remote_config ready bind=%s port=%u root=%s\n",
            bind_address, port, root);

    while (!g_stop) {
        int client;
        struct timeval timeout = {5, 0};
        client = accept(server, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) {
                if (g_stop) {
                    break;
                }
                continue;
            }
            close(server);
            return 1;
        }
        (void)setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        (void)setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        (void)handle_client(client, root, no_restart);
        close(client);
    }
    close(server);
    return 0;
}

static int validate_files(const char *commands_path, const char *targets_path)
{
    char *commands = NULL;
    char *targets = NULL;
    size_t commands_length;
    size_t targets_length;
    target_names target_list;
    size_t command_count;
    char error[256];

    if (read_file(commands_path, MAX_CONFIG, &commands, &commands_length) != 0 ||
        read_file(targets_path, MAX_CONFIG, &targets, &targets_length) != 0) {
        fprintf(stderr, "cannot read configuration files\n");
        free(commands);
        free(targets);
        return 1;
    }
    (void)commands_length;
    (void)targets_length;
    if (validate_targets(targets, &target_list, error, sizeof(error)) != 0 ||
        validate_commands(commands, &target_list, &command_count, error,
                          sizeof(error)) != 0) {
        fprintf(stderr, "validation failed: %s\n", error);
        free(commands);
        free(targets);
        return 1;
    }
    free(commands);
    free(targets);
    printf("VALID commands=%zu targets=%zu\n", command_count, target_list.count);
    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr,
            "usage:\n"
            "  %s serve [--root PATH] [--bind IPV4] [--port PORT] [--no-restart]\n"
            "  %s validate COMMANDS.tsv TARGETS.conf\n",
            program, program);
}

int main(int argc, char **argv)
{
    const char *root = DEFAULT_ROOT;
    const char *bind_address = DEFAULT_BIND;
    unsigned int port = DEFAULT_PORT;
    bool no_restart = false;
    int index;

    if (argc >= 2 && strcmp(argv[1], "validate") == 0) {
        if (argc != 4) {
            usage(argv[0]);
            return 2;
        }
        return validate_files(argv[2], argv[3]);
    }
    if (argc < 2 || strcmp(argv[1], "serve") != 0) {
        usage(argv[0]);
        return 2;
    }
    for (index = 2; index < argc; ++index) {
        if (strcmp(argv[index], "--root") == 0 && index + 1 < argc) {
            root = argv[++index];
        } else if (strcmp(argv[index], "--bind") == 0 && index + 1 < argc) {
            bind_address = argv[++index];
        } else if (strcmp(argv[index], "--port") == 0 && index + 1 < argc) {
            size_t parsed;
            if (!parse_size(argv[++index], 65535, &parsed) || parsed < 1024) {
                fprintf(stderr, "invalid port\n");
                return 2;
            }
            port = (unsigned int)parsed;
        } else if (strcmp(argv[index], "--no-restart") == 0) {
            no_restart = true;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (strlen(root) >= PATH_MAX) {
        fprintf(stderr, "root path too long\n");
        return 2;
    }
    return serve(root, bind_address, port, no_restart);
}
