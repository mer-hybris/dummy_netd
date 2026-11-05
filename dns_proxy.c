/*
 * Bionic getaddrinfo talks to /dev/socket/dnsproxyd. Answer with glibc
 * getaddrinfo so vendor processes use the host resolver.
 */

#include <glib.h>
#include <glib-unix.h>
#include <gutil_log.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#define DNS_PROXY_PATH "/dev/socket/dnsproxyd"
#define DNS_COMMAND_MAX 1024

/* SocketClient::sendCode is four bytes: three digits and a space. */
#define DNS_PROXY_QUERY_RESULT 222
/* bionic only checks that result code is 222
 * The content of 401 error result is missing from sent result */
#define DNS_PROXY_OPERATION_FAILED 401

#define BIONIC_AI_PASSIVE      0x00000001
#define BIONIC_AI_CANONNAME    0x00000002
#define BIONIC_AI_NUMERICHOST  0x00000004
#define BIONIC_AI_NUMERICSERV  0x00000008
#define BIONIC_AI_ALL          0x00000100
#define BIONIC_AI_V4MAPPED_CFG 0x00000200
#define BIONIC_AI_ADDRCONFIG   0x00000400
#define BIONIC_AI_V4MAPPED     0x00000800

#define DNS_CLIENT_MAX 4

static gint dns_client_count = 0;
static int dns_listen_fd = -1;
static guint dns_listen_source_id = 0;

static
int
convert_from_bionic_ai_flags(int bionic_flags) {
    int flags = 0;

    if (bionic_flags & BIONIC_AI_PASSIVE)
        flags |= AI_PASSIVE;
    if (bionic_flags & BIONIC_AI_CANONNAME)
        flags |= AI_CANONNAME;
    if (bionic_flags & BIONIC_AI_NUMERICHOST)
        flags |= AI_NUMERICHOST;
    if (bionic_flags & BIONIC_AI_NUMERICSERV)
        flags |= AI_NUMERICSERV;
    if (bionic_flags & BIONIC_AI_ALL)
        flags |= AI_ALL;
    if (bionic_flags & BIONIC_AI_ADDRCONFIG)
        flags |= AI_ADDRCONFIG;
    if ((bionic_flags & BIONIC_AI_V4MAPPED)
        || (bionic_flags & BIONIC_AI_V4MAPPED_CFG))
        flags |= AI_V4MAPPED;

    return flags;
}

static
int
convert_to_bionic_ai_flags(int flags) {
    int bionic_flags = 0;

    if (flags & AI_PASSIVE)
        bionic_flags |= BIONIC_AI_PASSIVE;
    if (flags & AI_CANONNAME)
        bionic_flags |= BIONIC_AI_CANONNAME;
    if (flags & AI_NUMERICHOST)
        bionic_flags |= BIONIC_AI_NUMERICHOST;
    if (flags & AI_NUMERICSERV)
        bionic_flags |= BIONIC_AI_NUMERICSERV;
    if (flags & AI_ALL)
        bionic_flags |= BIONIC_AI_ALL;
    if (flags & AI_ADDRCONFIG)
        bionic_flags |= BIONIC_AI_ADDRCONFIG;
    if (flags & AI_V4MAPPED)
        bionic_flags |= BIONIC_AI_V4MAPPED;

    return bionic_flags;
}

static
gboolean
send_all(
    int fd,
    const void* data,
    size_t len)
{
    const char* bytes = data;

    while (len > 0) {
        ssize_t wrote = send(fd, bytes, len, MSG_NOSIGNAL);

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
        !send_u32(fd, (guint32) convert_to_bionic_ai_flags(ai->ai_flags)) ||
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

    if (argc < 7) {
        send_code(fd, DNS_PROXY_OPERATION_FAILED);
        return;
    }

    host = optional_name(argv[1]);
    serv = optional_name(argv[2]);
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = convert_from_bionic_ai_flags(hint_field(argv[3]));
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
dns_client_worker(gpointer data)
{
    int fd = GPOINTER_TO_INT(data);
    char command[DNS_COMMAND_MAX];

    if (read_command(fd, command, sizeof(command)))
        dispatch_command(fd, command);

    close(fd);
    g_atomic_int_add(&dns_client_count, -1);
    return NULL;
}

static
gboolean
dns_listen_reader(gint listen_fd, GIOCondition condition, gpointer data) {
    if (condition & G_IO_IN) {
        int fd = accept(listen_fd, NULL, NULL);

        if (fd >= 0) {
            struct timeval timeout = { 5, 0 };
            GThread* thread;

            if (g_atomic_int_get(&dns_client_count) >= DNS_CLIENT_MAX ||
                setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                    &timeout, sizeof(timeout)) < 0 ||
                setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
                    &timeout, sizeof(timeout)) < 0) {
                close(fd);
                return G_SOURCE_CONTINUE;
            }

            g_atomic_int_inc(&dns_client_count);
            thread = g_thread_try_new("dns-client",
                dns_client_worker, GINT_TO_POINTER(fd), NULL);

            if (thread) {
                g_thread_unref(thread);
            } else {
                g_atomic_int_add(&dns_client_count, -1);
                close(fd);
            }
        }
    }
    return G_SOURCE_CONTINUE;
}

static
int
open_listener(void)
{
    struct sockaddr_un addr;
    int fd;

    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
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

    if (!g_unix_set_fd_nonblocking(dns_listen_fd, TRUE, NULL)) {
        close(dns_listen_fd);
        dns_listen_fd = -1;
        return FALSE;
    }

    dns_listen_source_id = g_unix_fd_add(dns_listen_fd, G_IO_IN,
        dns_listen_reader, NULL);

    GINFO("DNS proxy listening on %s", DNS_PROXY_PATH);
    return TRUE;
}

void
dns_proxy_stop(void)
{
    int fd = dns_listen_fd;

    if (dns_listen_source_id) {
        g_source_remove(dns_listen_source_id);
    }

    if (fd >= 0) {
        dns_listen_fd = -1;
        shutdown(fd, SHUT_RDWR);
        close(fd);
        unlink(DNS_PROXY_PATH);
    }
}
