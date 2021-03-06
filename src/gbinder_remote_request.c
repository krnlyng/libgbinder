/*
 * Copyright (C) 2018 Jolla Ltd.
 * Copyright (C) 2018 Slava Monich <slava.monich@jolla.com>
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
 *   3. Neither the name of Jolla Ltd nor the names of its contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
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

#include "gbinder_remote_request_p.h"
#include "gbinder_reader_p.h"
#include "gbinder_rpc_protocol.h"
#include "gbinder_object_registry.h"
#include "gbinder_buffer.h"
#include "gbinder_log.h"

#include <gutil_macros.h>

struct gbinder_remote_request {
    gint refcount;
    pid_t pid;
    uid_t euid;
    const GBinderRpcProtocol* protocol;
    const char* iface;
    char* iface2;
    gsize header_size;
    GBinderReaderData data;
};

GBinderRemoteRequest*
gbinder_remote_request_new(
    GBinderObjectRegistry* reg,
    const GBinderRpcProtocol* protocol,
    pid_t pid,
    uid_t euid)
{
    GBinderRemoteRequest* self = g_slice_new0(GBinderRemoteRequest);
    GBinderReaderData* data = &self->data;

    g_atomic_int_set(&self->refcount, 1);
    self->pid = pid;
    self->euid = euid;
    self->protocol = protocol;
    data->reg = gbinder_object_registry_ref(reg);
    return self;
}

static
void
gbinder_remote_request_free(
    GBinderRemoteRequest* self)
{
    GBinderReaderData* data = &self->data;

    gbinder_object_registry_unref(data->reg);
    gbinder_buffer_free(data->buffer);
    g_free(data->objects);
    g_free(self->iface2);
    g_slice_free(GBinderRemoteRequest, self);
}

static
inline
void
gbinder_remote_request_init_reader2(
    GBinderRemoteRequest* self,
    GBinderReader* p)
{
    /* The caller has already checked the request for NULL */
    GBinderReaderData* data = &self->data;
    GBinderBuffer* buffer = data->buffer;

    if (buffer) {
        gbinder_reader_init(p, data, self->header_size,
            buffer->size - self->header_size);
    } else {
        gbinder_reader_init(p, data, 0, 0);
    }
}

void
gbinder_remote_request_set_data(
    GBinderRemoteRequest* self,
    GBinderBuffer* buffer,
    void** objects)
{
    if (G_LIKELY(self)) {
        GBinderReaderData* data = &self->data;
        GBinderReader reader;

        g_free(self->iface2);
        g_free(data->objects);
        gbinder_buffer_free(data->buffer);
        data->buffer = buffer;
        data->objects = objects;

        /* Parse RPC header */
        self->header_size = 0;
        gbinder_remote_request_init_reader2(self, &reader);
        self->iface = self->protocol->read_rpc_header(&reader, &self->iface2);
        self->header_size = gbinder_reader_bytes_read(&reader);
    } else {
        gbinder_buffer_free(buffer);
        g_free(objects);
    }
}

const char*
gbinder_remote_request_interface(
    GBinderRemoteRequest* self)
{
    return G_LIKELY(self) ? self->iface : NULL;
}

GBinderRemoteRequest*
gbinder_remote_request_ref(
    GBinderRemoteRequest* self)
{
    if (G_LIKELY(self)) {
        GASSERT(self->refcount > 0);
        g_atomic_int_inc(&self->refcount);
    }
    return self;
}

void
gbinder_remote_request_unref(
    GBinderRemoteRequest* self)
{
    if (G_LIKELY(self)) {
        GASSERT(self->refcount > 0);
        if (g_atomic_int_dec_and_test(&self->refcount)) {
            gbinder_remote_request_free(self);
        }
    }
}

void
gbinder_remote_request_init_reader(
    GBinderRemoteRequest* self,
    GBinderReader* reader)
{
    if (G_LIKELY(self)) {
        gbinder_remote_request_init_reader2(self, reader);
    } else {
        gbinder_reader_init(reader, NULL, 0, 0);
    }
}

pid_t
gbinder_remote_request_sender_pid(
    GBinderRemoteRequest* self)
{
    return G_LIKELY(self) ? self->pid : (uid_t)(-1);
}

uid_t
gbinder_remote_request_sender_euid(
    GBinderRemoteRequest* self)
{
    return G_LIKELY(self) ? self->euid : (uid_t)(-1);
}

gboolean
gbinder_remote_request_read_int32(
    GBinderRemoteRequest* self,
    gint32* value)
{
    return gbinder_remote_request_read_uint32(self, (guint32*)value);
}

gboolean
gbinder_remote_request_read_uint32(
    GBinderRemoteRequest* self,
    guint32* value)
{
    if (G_LIKELY(self)) {
        GBinderReader reader;

        gbinder_remote_request_init_reader2(self, &reader);
        return gbinder_reader_read_uint32(&reader, value);
    }
    return FALSE;
}

gboolean
gbinder_remote_request_read_int64(
    GBinderRemoteRequest* self,
    gint64* value)
{
    return gbinder_remote_request_read_uint64(self, (guint64*)value);
}

gboolean
gbinder_remote_request_read_uint64(
    GBinderRemoteRequest* self,
    guint64* value)
{
    if (G_LIKELY(self)) {
        GBinderReader reader;

        gbinder_remote_request_init_reader2(self, &reader);
        return gbinder_reader_read_uint64(&reader, value);
    }
    return FALSE;
}

const char*
gbinder_remote_request_read_string8(
    GBinderRemoteRequest* self)
{
    if (G_LIKELY(self)) {
        GBinderReader reader;

        gbinder_remote_request_init_reader2(self, &reader);
        return gbinder_reader_read_string8(&reader);
    }
    return NULL;
}

char*
gbinder_remote_request_read_string16(
    GBinderRemoteRequest* self)
{
    if (G_LIKELY(self)) {
        GBinderReader reader;

        gbinder_remote_request_init_reader2(self, &reader);
        return gbinder_reader_read_string16(&reader);
    }
    return NULL;
}

GBinderRemoteObject*
gbinder_remote_request_read_object(
    GBinderRemoteRequest* self)
{
    if (G_LIKELY(self)) {
        GBinderReader reader;

        gbinder_remote_request_init_reader2(self, &reader);
        return gbinder_reader_read_object(&reader);
    }
    return NULL;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
