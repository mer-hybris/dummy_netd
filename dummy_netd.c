/*
 * Copyright (C) 2019-2020 Jolla Ltd.
 * Copyright (C) 2019 Franz-Josef Haider <franz.haider@jolla.com>
 *
 * You may use this file under the terms of BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *   3. Neither the names of the copyright holders nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <glib.h>
#include <glib-unix.h>
#include <gbinder.h>
#include <gutil_log.h>
#include <systemd/sd-daemon.h>
#include <stdio.h>

#define BINDER_DEVICE "/dev/hwbinder"
#define NETD_IFACE "android.system.net.netd@1.1::INetd"
#define NETD_SLOT "default"

#define RET_OK      0
#define RET_CMDLINE 1
#define RET_ERR     2

static
gboolean
netd_signal(
    gpointer user_data)
{
    GINFO("Signal caught, exiting...");
    g_main_loop_quit((GMainLoop*) user_data);
    return G_SOURCE_CONTINUE;
}

static
GBinderLocalReply*
netd_handler(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    GDEBUG("%s %d", gbinder_remote_request_interface(req), code);
    return NULL;
}

static
gboolean
netd_opt_verbose(
    const gchar* name,
    const gchar* value,
    gpointer data,
    GError** error)
{
    gutil_log_default.level = (gutil_log_default.level < GLOG_LEVEL_DEBUG) ?
        GLOG_LEVEL_DEBUG : GLOG_LEVEL_VERBOSE;
    return TRUE;
}

static
int
netd_run(
    const char* dev)
{
    GBinderServiceManager* svcmgr = gbinder_servicemanager_new(dev);

    if (svcmgr) {
        GMainLoop* loop = g_main_loop_new(NULL, TRUE);
        guint sigterm = g_unix_signal_add(SIGTERM, netd_signal, loop);
        guint sigint = g_unix_signal_add(SIGINT, netd_signal, loop);
        GBinderLocalObject* obj = gbinder_servicemanager_new_local_object
            (svcmgr, NETD_IFACE, netd_handler, NULL);
        GBinderServiceName* name = gbinder_servicename_new
            (svcmgr, obj, NETD_SLOT);

        /* Do we need to wait until the name is actually registered? */
        sd_notify(0, "READY=1");

        /* Run the event loop */
        g_main_loop_run(loop);

        /* Exiting on signal */
        g_source_remove(sigterm);
        g_source_remove(sigint);
        g_main_loop_unref(loop);

        gbinder_servicename_unref(name);
        gbinder_servicemanager_unref(svcmgr);
        gbinder_local_object_drop(obj);
        return RET_OK;
    } else {
        /* libgbinder prints the error in this case */
        return RET_ERR;
    }
}

int main(int argc, char* argv[])
{
    int ret = RET_CMDLINE;
    char* dev = NULL;
    GError* error = NULL;
    GOptionContext* options = g_option_context_new(NULL);
    GOptionEntry entries[] = {
        { "verbose", 'v', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
           netd_opt_verbose, "Enable debug output", NULL },
        { "device", 'd', 0, G_OPTION_ARG_STRING, &dev,
          "Binder device [" BINDER_DEVICE "]", "DEVICE" },
        { NULL }
    };

    gutil_log_default.name = "dummy_netd";
    gutil_log_timestamp = FALSE;
    g_option_context_add_main_entries(options, entries, NULL);
    if (g_option_context_parse(options, &argc, &argv, &error)) {
        if (argc > 1) {
            char* help = g_option_context_get_help(options, TRUE, NULL);

            fprintf(stderr, "%s", help);
            g_free(help);
        } else {
            ret = netd_run((dev && dev[0]) ? dev : BINDER_DEVICE);
        }
    } else {
        GERR("%s", error->message);
        g_error_free(error);
    }
    g_option_context_free(options);
    g_free(dev);
    return ret;
}
