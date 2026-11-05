/*
 * Bionic getaddrinfo talks to /dev/socket/dnsproxyd. Answer with glibc
 * getaddrinfo so vendor processes use the host resolver.
 */

#include <glib.h>
#include <gutil_log.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define DNS_PROXY_PATH "/dev/socket/dnsproxyd"
#define DNS_COMMAND_MAX 1024

/* SocketClient::sendCode is four bytes: three digits and a space. */
#define DNS_PROXY_QUERY_RESULT 222
#define DNS_PROXY_OPERATION_FAILED 401

static int dns_listen_fd = -1;
static GThread* dns_listen_thread = NULL;

static
gboolean
send_all(
    int fd,
    const void* data,
    size_t len)
{
    const char* bytes = data;

    while (len > 0) {
        ssize_t wrote = write(fd, bytes, len);

        if (wrote < 0) {
            if (errno == EINTR)
                continue;
            return FALSE;
        }
        if (wrote == 0)
            return FALSE;
        bytes += wrote;
        len -= (size_t) wrote;
    }
    return TRUE;
}

static
gboolean
send_u32(
    int fd,
    guint32 value)
{
    guint32 be = htonl(value);

    return send_all(fd, &be, sizeof(be));
}

static
gboolean
send_code(
    int fd,
    int code)
{
    char buf[4];

    snprintf(buf, sizeof(buf), "%03d", code);
    buf[3] = ' ';
    return send_all(fd, buf, sizeof(buf));
}

static
gboolean
read_command(
    int fd,
    char* buf,
    size_t buflen)
{
    size_t used = 0;

    while (used + 1 < buflen) {
        ssize_t got = read(fd, buf + used, 1);

        if (got < 0) {
            if (errno == EINTR)
                continue;
            return FALSE;
        }
        if (got == 0)
            return FALSE;
        if (buf[used] == '\0')
            return used > 0;
        used++;
    }
    return FALSE;
}

static
int
hint_field(
    const char* text)
{
    int value = atoi(text);

    return value == -1 ? 0 : value;
}

static
const char*
optional_name(
    const char* text)
{
    return g_strcmp0(text, "^") == 0 ? NULL : text;
}

static
gboolean
send_addrinfo(
    int fd,
    const struct addrinfo* ai)
{
    guint32 name_len = 0;

    if (!send_u32(fd, 1) ||
        !send_u32(fd, (guint32) ai->ai_flags) ||
        !send_u32(fd, (guint32) ai->ai_family) ||
        !send_u32(fd, (guint32) ai->ai_socktype) ||
        !send_u32(fd, (guint32) ai->ai_protocol) ||
        !send_u32(fd, (guint32) ai->ai_addrlen))
        return FALSE;
    if (ai->ai_addrlen > 0 && !send_all(fd, ai->ai_addr, ai->ai_addrlen))
        return FALSE;
    if (ai->ai_canonname)
        name_len = (guint32) strlen(ai->ai_canonname) + 1;
    if (!send_u32(fd, name_len))
        return FALSE;
    if (name_len > 0 && !send_all(fd, ai->ai_canonname, name_len))
        return FALSE;
    return TRUE;
}

static
void
proxy_getaddrinfo(
    int fd,
    char** argv,
    int argc)
{
    struct addrinfo hints;
    struct addrinfo* res = NULL;
    struct addrinfo* ai;
    const char* host;
    const char* serv;
    int rc;

    if (argc != 8) {
        send_code(fd, DNS_PROXY_OPERATION_FAILED);
        return;
    }

    host = optional_name(argv[1]);
    serv = optional_name(argv[2]);
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = hint_field(argv[3]);
    hints.ai_family = hint_field(argv[4]);
    hints.ai_socktype = hint_field(argv[5]);
    hints.ai_protocol = hint_field(argv[6]);

    rc = getaddrinfo(host, serv, &hints, &res);
    if (rc != 0) {
        send_code(fd, DNS_PROXY_OPERATION_FAILED);
        return;
    }

    if (send_code(fd, DNS_PROXY_QUERY_RESULT)) {
        for (ai = res; ai; ai = ai->ai_next) {
            if (!send_addrinfo(fd, ai))
                break;
        }
        send_u32(fd, 0);
    }
    freeaddrinfo(res);
}

static
void
dispatch_command(
    int fd,
    char* command)
{
    char** argv = g_strsplit(command, " ", -1);
    int argc = (int) g_strv_length(argv);

    if (argc > 0 && g_strcmp0(argv[0], "getaddrinfo") == 0)
        proxy_getaddrinfo(fd, argv, argc);
    g_strfreev(argv);
}

static
gpointer
listen_thread(
    gpointer data)
{
    int listen_fd = GPOINTER_TO_INT(data);

    for (;;) {
        int fd = accept(listen_fd, NULL, NULL);
        char command[DNS_COMMAND_MAX];

        if (fd < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (read_command(fd, command, sizeof(command)))
            dispatch_command(fd, command);
        close(fd);
    }
    return NULL;
}

static
int
open_listener(void)
{
    struct sockaddr_un addr;
    int fd;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        GERR("dns proxy socket: %s", strerror(errno));
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, DNS_PROXY_PATH, sizeof(addr.sun_path) - 1);
    unlink(DNS_PROXY_PATH);
    if (bind(fd, (struct sockaddr*) &addr, sizeof(addr)) != 0 ||
        chmod(DNS_PROXY_PATH, 0660) != 0 ||
        listen(fd, 8) != 0) {
        GERR("%s: %s", DNS_PROXY_PATH, strerror(errno));
        close(fd);
        unlink(DNS_PROXY_PATH);
        return -1;
    }
    return fd;
}

gboolean
dns_proxy_start(void)
{
    dns_listen_fd = open_listener();
    if (dns_listen_fd < 0)
        return FALSE;

    dns_listen_thread = g_thread_new("dns-proxy", listen_thread,
        GINT_TO_POINTER(dns_listen_fd));
    GINFO("DNS proxy listening on %s", DNS_PROXY_PATH);
    return TRUE;
}

void
dns_proxy_stop(void)
{
    int fd = dns_listen_fd;

    if (fd >= 0) {
        dns_listen_fd = -1;
        shutdown(fd, SHUT_RDWR);
        close(fd);
        unlink(DNS_PROXY_PATH);
    }
    if (dns_listen_thread) {
        g_thread_join(dns_listen_thread);
        dns_listen_thread = NULL;
    }
}
