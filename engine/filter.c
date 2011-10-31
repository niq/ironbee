/*****************************************************************************
 * Licensed to Qualys, Inc. (QUALYS) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * QUALYS licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

/**
 * @file
 * @brief IronBee - Filter Interface
 *
 * @author Brian Rectanus <brectanus@qualys.com>
 */

#include "ironbee_config_auto.h"

#include <string.h>

#include <ironbee/engine.h>
#include <ironbee/util.h>

#include "ironbee_private.h"

ib_status_t ib_fctl_tx_create(ib_fctl_t **pfc,
                              ib_tx_t *tx,
                              ib_mpool_t *pool)
{
    IB_FTRACE_INIT(ib_fctl_tx_create);
//    ib_engine_t *ib = tx->ib;
    ib_status_t rc = IB_OK;

    /* Create the main structure. */
    *pfc = (ib_fctl_t *)ib_mpool_calloc(pool, 1, sizeof(**pfc));
    if (*pfc == NULL) {
        rc = IB_EALLOC;
        goto failed;
    }
    (*pfc)->ib = tx->ib;
    (*pfc)->mp = pool;
    (*pfc)->fdata.udata.tx = tx;

    /* Create streams */
    /// @todo Needs a create method
    (*pfc)->source = (ib_stream_t *)ib_mpool_calloc(pool, 1, sizeof(*(*pfc)->source));
    if ((*pfc)->source == NULL) {
        rc = IB_EALLOC;
        goto failed;
    }
    (*pfc)->sink = (ib_stream_t *)ib_mpool_calloc(pool, 1, sizeof(*(*pfc)->sink));
    if ((*pfc)->sink == NULL) {
        rc = IB_EALLOC;
        goto failed;
    }
    (*pfc)->source->mp = pool;
    (*pfc)->sink->mp = pool;

    IB_FTRACE_RET_STATUS(rc);

failed:
    /* Make sure everything is cleaned up on failure */
    *pfc = NULL;

    IB_FTRACE_RET_STATUS(rc);
}

ib_status_t ib_fctl_config(ib_fctl_t *fc,
                           ib_context_t *ctx)
{
    IB_FTRACE_INIT(ib_fctl_config);
//    ib_engine_t *ib = fc->ib;
    ib_status_t rc;

    /* Use filters from context. */
    fc->filters = ctx->filters;

    rc = ib_fctl_process(fc);
    IB_FTRACE_RET_STATUS(rc);
}

static ib_status_t _filter_exec(ib_filter_t *f,
                                ib_fdata_t *fdata)
{
    IB_FTRACE_INIT(_filter_exec);
//    ib_engine_t *ib = f->ib;
    ib_context_t *ctx;
    ib_mpool_t *pool;
    ib_flags_t flags;
    ib_status_t rc;

    switch (f->type) {
        case IB_FILTER_TX:
            ctx = fdata->udata.tx->ctx;
            pool = fdata->udata.tx->mp;
            break;
        case IB_FILTER_CONN:
            ctx = fdata->udata.conn->ctx;
            pool = fdata->udata.conn->mp;
            break;
        default:
            IB_FTRACE_RET_STATUS(IB_EINVAL);
    }

    rc = f->fn_filter(f, fdata, ctx, pool, &flags);
    if (rc != IB_OK) {
        IB_FTRACE_RET_STATUS(rc);
    }

    /// @todo Handle flags

    IB_FTRACE_RET_STATUS(rc);
}

ib_status_t ib_fctl_process(ib_fctl_t *fc)
{
    IB_FTRACE_INIT(ib_fctl_process);
    ib_engine_t *ib = fc->ib;
    ib_list_node_t *node;
    ib_status_t rc = IB_OK;

    if (fc->filters == NULL) {
        IB_FTRACE_RET_STATUS(IB_OK);
    }

    /* Prepare data for filtering. */
    fc->fdata.stream = fc->source;

    /* Filter if there are filters. */
    if (ib_list_elements(fc->filters) > 0) {
        IB_LIST_LOOP(fc->filters, node) {
            ib_filter_t *f = (ib_filter_t *)ib_list_node_data(node);


            rc = _filter_exec(f, &fc->fdata);
            if (rc != IB_OK) {
                /// @todo Handle errors
                ib_log_error(ib, 3,
                             "Error processing filter idx=%d \"%s\": %d",
                             f->idx, f->name, rc);
            }
        }
    }

    /* Buffer if there is a buffer filter. */
    if (fc->fbuffer) {
        rc = _filter_exec(fc->fbuffer, &fc->fdata);
        if (rc != IB_OK) {
            IB_FTRACE_RET_STATUS(rc);
        }
    }

    /* Move anything remaining in the stream to the sink. */
    if (fc->fdata.stream->nelts) {
        ib_sdata_t *sdata;

        /// @todo Need API to move data between streams.
        rc = ib_stream_pull(fc->fdata.stream, &sdata);
        while (rc == IB_OK) {
            rc = ib_stream_push_sdata(fc->sink, sdata);
            if (rc == IB_OK) {
                rc = ib_stream_pull(fc->fdata.stream, &sdata);
            }
        }
        if (rc != IB_ENOENT) {
            IB_FTRACE_RET_STATUS(rc);
        }
    }
    

    IB_FTRACE_RET_STATUS(rc);
}

ib_status_t ib_fctl_data(ib_fctl_t *fc,
                         ib_data_type_t dtype,
                         void *data,
                         size_t dlen)
{
    IB_FTRACE_INIT(ib_fctl_data);
//    ib_engine_t *ib = fc->ib;
    ib_status_t rc;

    rc = ib_stream_push(fc->source, IB_STREAM_DATA, dtype, data, dlen);
    if (rc != IB_OK) {
        IB_FTRACE_RET_STATUS(rc);
    }

    rc = ib_fctl_process(fc);
    IB_FTRACE_RET_STATUS(rc);
}

ib_status_t ib_fctl_meta(ib_fctl_t *fc,
                         ib_sdata_type_t stype)
{
    IB_FTRACE_INIT(ib_fctl_meta);
//    ib_engine_t *ib = fc->ib;
    ib_status_t rc;

    rc = ib_stream_push(fc->source, stype, IB_DTYPE_META, NULL, 0);
    if (rc != IB_OK) {
        IB_FTRACE_RET_STATUS(rc);
    }

    rc = ib_fctl_process(fc);
    IB_FTRACE_RET_STATUS(rc);
}

ib_status_t ib_fctl_drain(ib_fctl_t *fc,
                          ib_stream_t **pstream)
{
    IB_FTRACE_INIT(ib_fctl_drain);

    if (pstream != NULL) {
        *pstream = fc->sink;
    }

    IB_FTRACE_RET_STATUS(IB_OK);
}

ib_status_t ib_stream_push_sdata(ib_stream_t *s,
                                 ib_sdata_t *sdata)
{
    IB_FTRACE_INIT(ib_stream_push_sdata);

    s->slen += sdata->dlen;

    if (s->nelts == 0) {
        IB_LIST_NODE_INSERT_INITIAL(s, sdata);
        IB_FTRACE_RET_STATUS(IB_OK);
    }

    IB_LIST_NODE_INSERT_LAST(s, sdata, ib_sdata_t);

    IB_FTRACE_RET_STATUS(IB_OK);
}
                           
ib_status_t ib_stream_push(ib_stream_t *s,
                           ib_sdata_type_t type,
                           ib_data_type_t dtype,
                           void *data,
                           size_t dlen)
{
    IB_FTRACE_INIT(ib_stream_push);
    /// @todo take from a resource pool, if available
    ib_sdata_t *node = (ib_sdata_t *)ib_mpool_calloc(s->mp,
                                                     1, sizeof(*node));
    if (node == NULL) {
        IB_FTRACE_RET_STATUS(IB_EALLOC);
    }

    node->type = type;
    node->dtype = dtype;
    node->dlen = dlen;
    node->data = data;

    ib_stream_push_sdata(s, node);

    IB_FTRACE_RET_STATUS(IB_OK);
}
                           
ib_status_t ib_stream_pull(ib_stream_t *s,
                           ib_sdata_t **psdata)
{
    IB_FTRACE_INIT(ib_stream_pull);

    if (s->nelts == 0) {
        if (psdata != NULL) {
            *psdata = NULL;
        }
        IB_FTRACE_RET_STATUS(IB_ENOENT);
    }

    s->slen -= s->head->dlen;
    if (psdata != NULL) {
        *psdata = s->head;
    }

    IB_LIST_NODE_REMOVE_FIRST(s);


    IB_FTRACE_RET_STATUS(IB_OK);
}

ib_status_t ib_filter_register(ib_filter_t **pf,
                               ib_engine_t *ib,
                               const char *name,
                               ib_filter_type_t type,
                               ib_flags_t options,
                               ib_filter_fn_t fn_filter,
                               void *cbdata)
{
    IB_FTRACE_INIT(ib_filter_register);
    ib_status_t rc;

    *pf = (ib_filter_t *)ib_mpool_calloc(ib->mp, 1, sizeof(**pf));
    if (*pf == NULL) {
        IB_FTRACE_RET_STATUS(IB_EALLOC);
    }

    (*pf)->ib = ib;
    (*pf)->name = ib_mpool_strdup(ib->mp, name);
    (*pf)->type = type;
    (*pf)->options = options;
    (*pf)->idx = ib_array_elements(ib->filters);
    (*pf)->fn_filter = fn_filter;
    (*pf)->cbdata = cbdata;

    rc = ib_array_setn(ib->filters, (*pf)->idx, *pf);
    if (rc != IB_OK) {
        ib_log_error(ib, 1, "Failed to register filter %s %d", (*pf)->name, rc);
        *pf = NULL;
        IB_FTRACE_RET_STATUS(rc);
    }

    IB_FTRACE_RET_STATUS(rc);
}

