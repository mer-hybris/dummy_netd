/*
 * Copyright (C) 2019 Jolla Ltd.
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

#include <stdio.h>
#include <gbinder.h>
#include <systemd/sd-daemon.h>

#define BINDER_DEVICE "/dev/hwbinder"
#define NETD_IFACE "android.system.net.netd@1.1::INetd"
#define NETD_SLOT "default"

static
GBinderLocalReply*
netd_reply(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    fprintf(stderr, "netd_reply called with on interface: %s, code: %d, flags: %d\n",
                    gbinder_remote_request_interface(req), code, flags);
    return NULL;
}

static
void
add_service_done(
    GBinderServiceManager* sm,
    int status,
    void* user_data)
{
    if (status == GBINDER_STATUS_OK) {
        sd_notify(0, "READY=1");
    } else {
	g_main_loop_quit(user_data);
    }
}

int main(int argc, char **argv) {
    GMainLoop *loop;
    GBinderServiceManager *svcmgr;
    GBinderLocalObject *obj;

    loop = g_main_loop_new(NULL, TRUE);

    svcmgr = gbinder_servicemanager_new(BINDER_DEVICE);

    obj = gbinder_servicemanager_new_local_object(svcmgr, NETD_IFACE, netd_reply, loop);
    gbinder_servicemanager_add_service(svcmgr, NETD_SLOT, obj, add_service_done, loop);

    g_main_loop_run(loop);

    gbinder_local_object_unref(obj);
    gbinder_servicemanager_unref(svcmgr);

    g_main_loop_unref(loop);

    return 0;
}

