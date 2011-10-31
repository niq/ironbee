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
 * @brief IronBee - HTP Module
 *
 * This module integrates libhtp.
 *
 * @author Brian Rectanus <brectanus@qualys.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <ctype.h>

#include <ironbee/engine.h>
#include <ironbee/util.h>
#include <ironbee/module.h>
#include <ironbee/provider.h>

#include <htp.h>
#include <dslib.h>


/* Define the module name as well as a string version of it. */
#define MODULE_NAME        htp
#define MODULE_NAME_STR    IB_XSTRINGIFY(MODULE_NAME)

/* Define the public module symbol. */
IB_MODULE_DECLARE();

typedef struct modhtp_context_t modhtp_context_t;
typedef struct modhtp_cfg_t modhtp_cfg_t;
typedef struct modhtp_nameval_t modhtp_nameval_t;

/** Module Context Structure */
struct modhtp_context_t {
    ib_engine_t    *ib;           /**< Engine handle */
    ib_conn_t      *iconn;        /**< Connection structure */
    modhtp_cfg_t   *modcfg;       /**< Module config structure */
    htp_cfg_t      *htp_cfg;      /**< Parser config handle */
    htp_connp_t    *htp;          /**< Parser handle */
};

/** Module Configuration Structure */
struct modhtp_cfg_t {
    char           *personality;  /**< libhtp personality */
};

/* Instantiate a module global configuration. */
static modhtp_cfg_t modhtp_global_cfg;

/* -- libhtp Routines -- */

/* Define a name/val lookup record. */
struct modhtp_nameval_t {
    const char *name;
    int         val;
};

/* Text versions of personalities */
static const modhtp_nameval_t modhtp_personalities[] = {
    { "",           HTP_SERVER_IDS },
    { "minimal",    HTP_SERVER_MINIMAL },
    { "generic",    HTP_SERVER_GENERIC },
    { "ids",        HTP_SERVER_IDS },
    { "iis_4_0",    HTP_SERVER_IIS_4_0 },
    { "iis_5_0",    HTP_SERVER_IIS_5_0 },
    { "iis_5_1",    HTP_SERVER_IIS_5_1 },
    { "iis_6_0",    HTP_SERVER_IIS_6_0 },
    { "iis_7_0",    HTP_SERVER_IIS_7_0 },
    { "iis_7_5",    HTP_SERVER_IIS_7_5 },
    { "tomcat_6_0", HTP_SERVER_TOMCAT_6_0 },
    { "apache",     HTP_SERVER_APACHE },
    { "apache_2_2", HTP_SERVER_APACHE_2_2 },
    { NULL, 0 }
};

/* Lookup a numeric personality from a name. */
static int modhtp_personality(const char *name)
{
    IB_FTRACE_INIT(modhtp_personality);
    const modhtp_nameval_t *rec = modhtp_personalities;

    if (name == NULL) {
        IB_FTRACE_RET_INT(-1);
    }

    while (rec->name != NULL) {
        if (strcasecmp(name, rec->name) == 0) {
            IB_FTRACE_RET_INT(rec->val);
        }

        rec++;
    }

    IB_FTRACE_RET_INT(-1);
}

/* Log htp data via ironbee logging. */
static int modhtp_callback_log(htp_log_t *log)
{
    IB_FTRACE_INIT(modhtp_callback_log);
    modhtp_context_t *modctx =
        (modhtp_context_t *)htp_connp_get_user_data(log->connp);
    int level;

    switch(log->level) {
        case HTP_LOG_ERROR:
            level = 1;
            break;
        case HTP_LOG_WARNING:
            level = 4;
            break;
        case HTP_LOG_NOTICE:
            level = 4;
            break;
        case HTP_LOG_INFO:
            level = 5;
            break;
        case HTP_LOG_DEBUG:
            level = 6;
            break;
        default:
            level = 9;
    }

    /* Log errors with the error code, otherwise it is just debug. */
    if (log->code != 0) {
        ib_log_debug(modctx->ib, level, "LibHTP [error %d] %s",
                     log->code, log->msg);
    }
    else {
        ib_log_debug(modctx->ib, level, "LibHTP %s", log->msg);
    }

    IB_FTRACE_RET_INT(0);
}


/* -- Field Generation Routines -- */


static ib_status_t modhtp_field_gen_bytestr(ib_provider_inst_t *dpi,
                                            const char *name,
                                            bstr *bs,
                                            ib_field_t **pf)
{
    ib_status_t rc;

    if (bs == NULL) {
        if (pf != NULL) {
            *pf = NULL;
        }
        return IB_EINVAL;
    }

    rc = ib_data_add_bytestr_ex(dpi, name, strlen(name),
                                (uint8_t *)bstr_ptr(bs),
                                bstr_len(bs), pf);
    if (rc != IB_OK) {
        ib_log_error(dpi->pr->ib, 4, "Failed to generate \"%s\" field: %d", name, rc);
    }

    return rc;
}

#define modhtp_field_gen_list(dpi,name,pf) \
    ib_data_add_list_ex(dpi,name,strlen(name),pf)


/* -- LibHTP Callbacks -- */

static int modhtp_htp_tx_start(htp_connp_t *connp)
{
    IB_FTRACE_INIT(modhtp_htp_tx_start);
    modhtp_context_t *modctx = htp_connp_get_user_data(connp);
    ib_conn_t *iconn = modctx->iconn;
    ib_engine_t *ib = iconn->ib;
    ib_tx_t *itx;
    ib_status_t rc;
    htp_tx_t *tx;

    /* Create the transaction structure. */
    ib_log_debug(ib, 9, "Creating transaction structure");
    rc = ib_tx_create(ib, &itx, iconn, NULL);
    if (rc != IB_OK) {
        /// @todo Set error.
        IB_FTRACE_RET_INT(HTP_ERROR);
    }

    /* Store this as the current transaction. */
    /* Use the current parser transaction to generate fields. */
    ib_log_debug(ib, 9, "LIBHTP: state=%d", connp->in_status);
    if (connp->in_status == STREAM_STATE_ERROR) {
        ib_log_error(ib, 3, "HTP Parser Error");
    }
    tx = connp->in_tx;
    if (tx == NULL) {
        /// @todo Set error.
        IB_FTRACE_RET_INT(HTP_ERROR);
    }

    /* Associate the ironbee transaction with the libhtp transaction. */
    htp_tx_set_user_data(tx, itx);

    /* Tell the engine that the request started. */
    ib_state_notify_request_started(ib, itx);

    IB_FTRACE_RET_INT(HTP_OK);
}

static int modhtp_htp_request_line(htp_connp_t *connp)
{
    IB_FTRACE_INIT(modhtp_htp_request_line);
    modhtp_context_t *modctx = htp_connp_get_user_data(connp);
    htp_tx_t *tx = connp->in_tx;
    ib_conn_t *iconn = modctx->iconn;
    ib_engine_t *ib = iconn->ib;
    ib_txdata_t itxdata;
    ib_tx_t *itx;

    /* Use the current parser transaction to generate fields. */
    ib_log_debug(ib, 9, "LIBHTP: state=%d", connp->in_status);
    if (connp->in_status == STREAM_STATE_ERROR) {
        ib_log_error(ib, 3, "HTP Parser Error");
    }
    if (tx == NULL) {
        /// @todo Set error.
        IB_FTRACE_RET_INT(HTP_ERROR);
    }
    if (tx->flags) {
        ib_log_error(ib, 4, "HTP parser flagged an event: 0x%08x", tx->flags);
        /// @todo Do something about it
    }

    /* Fetch the ironbee transaction and notify the engine
     * that more transaction data has arrived.
     */
    itx = htp_tx_get_user_data(tx);


    /* Store the transaction URI path. */
    if ((tx->parsed_uri != NULL) && (tx->parsed_uri->path != NULL)) {
        itx->path = (const char *)bstr_util_strdup_to_c(tx->parsed_uri->path);
    }
    if (itx->path == NULL) {
        ib_log_debug(ib, 4, "Unknown URI path - using /");
        /// @todo Probably should set a flag here
        itx->path = (const char *)ib_mpool_strdup(itx->mp, "/");
    }

    /* Store the hostname if it was parsed with the URI. */
    if ((tx->parsed_uri != NULL) && (tx->parsed_uri->hostname != NULL)) {
        itx->hostname = (const char *)bstr_util_strdup_to_c(tx->parsed_uri->hostname);
    }
    if (itx->hostname == NULL) {
        ib_log_debug(ib, 4, "Unknown hostname - using ip: %s", iconn->local_ipstr);
        /// @todo Probably should set a flag here
        itx->hostname = (const char *)ib_mpool_strdup(itx->mp,
                                                      iconn->local_ipstr);
    }

    /* Fill in a temporary ib_txdata_t structure and use it
     * to notify the engine of transaction data.
     */
    itxdata.ib = ib;
    itxdata.mp = itx->mp;
    itxdata.tx = itx;
    itxdata.dtype = IB_DTYPE_HTTP_LINE;
    itxdata.dalloc = bstr_size(tx->request_line_raw);
    itxdata.dlen = bstr_len(tx->request_line_raw);
    itxdata.data = (uint8_t *)bstr_ptr(tx->request_line_raw);

    ib_state_notify_tx_data_in(ib, &itxdata);

    IB_FTRACE_RET_INT(HTP_OK);
}

static int modhtp_htp_request_headers(htp_connp_t *connp)
{
    IB_FTRACE_INIT(modhtp_htp_request_headers);
    modhtp_context_t *modctx = htp_connp_get_user_data(connp);
    htp_tx_t *tx = connp->in_tx;
    htp_header_line_t *hline;
    ib_conn_t *iconn = modctx->iconn;
    ib_engine_t *ib = iconn->ib;
    ib_txdata_t itxdata;
    ib_tx_t *itx;

    /* Use the current parser transaction to generate fields. */
    ib_log_debug(ib, 9, "LIBHTP: state=%d", connp->in_status);
    if (connp->in_status == STREAM_STATE_ERROR) {
        ib_log_error(ib, 3, "HTP Parser Error");
    }
    if (tx == NULL) {
        /// @todo Set error.
        IB_FTRACE_RET_INT(HTP_ERROR);
    }
    if (tx->flags) {
        ib_log_error(ib, 4, "HTP parser flagged an event: 0x%08x", tx->flags);
        /// @todo Do something about it
    }


    /* Fetch the ironbee transaction and notify the engine
     * that the request headers are now available.
     */
    itx = htp_tx_get_user_data(tx);

    /* Update the hostname that may have changed with headers. */
    if ((tx->parsed_uri != NULL) && (tx->parsed_uri->hostname != NULL)) {
        itx->hostname = (const char *)bstr_util_strdup_to_c(tx->parsed_uri->hostname);
    }
    if (itx->hostname == NULL) {
        ib_log_debug(ib, 4, "Unknown hostname - using ip: %s", iconn->local_ipstr);
        /// @todo Probably should set a flag here
        itx->hostname = (const char *)ib_mpool_strdup(itx->mp,
                                                      iconn->local_ipstr);
    }

    /* Fill in a temporary ib_txdata_t structure for each header line
     * and use it to notify the engine of transaction data.
     */
    itxdata.ib = ib;
    itxdata.mp = itx->mp;
    itxdata.tx = itx;
    itxdata.dtype = IB_DTYPE_HTTP_HEADER;
    list_iterator_reset(tx->request_header_lines);
    while ((hline = list_iterator_next(tx->request_header_lines)) != NULL) {
        itxdata.dalloc = bstr_size(hline->line);
        itxdata.dlen = bstr_len(hline->line);
        itxdata.data = (uint8_t *)bstr_ptr(hline->line);

        ib_state_notify_tx_data_in(ib, &itxdata);
    }

    /* Headers separator */
    itxdata.dalloc = bstr_size(tx->request_headers_sep);
    itxdata.dlen = bstr_len(tx->request_headers_sep);
    itxdata.data = (uint8_t *)bstr_ptr(tx->request_headers_sep);
    ib_state_notify_tx_data_in(ib, &itxdata);

    /* The full headers are now available. */
    ib_state_notify_request_headers(ib, itx);

    IB_FTRACE_RET_INT(HTP_OK);
}

static int modhtp_htp_request_body_data(htp_tx_data_t *txdata)
{
    IB_FTRACE_INIT(modhtp_htp_body_data);
    htp_connp_t *connp = txdata->tx->connp;
    modhtp_context_t *modctx = htp_connp_get_user_data(connp);
    htp_tx_t *tx = connp->in_tx;
    ib_conn_t *iconn = modctx->iconn;
    ib_engine_t *ib = iconn->ib;
    ib_txdata_t itxdata;
    ib_tx_t *itx;

    /* Use the current parser transaction to generate fields. */
    ib_log_debug(ib, 9, "LIBHTP: state=%d", connp->in_status);
    if (connp->in_status == STREAM_STATE_ERROR) {
        ib_log_error(ib, 3, "HTP Parser Error");
    }
    if (tx == NULL) {
        /// @todo Set error.
        IB_FTRACE_RET_INT(HTP_ERROR);
    }
    if (tx->flags) {
        ib_log_error(ib, 4, "HTP parser flagged an event: 0x%08x", tx->flags);
        /// @todo Do something about it
    }


    /* Fetch the ironbee transaction and notify the engine
     * that more transaction data has arrived.
     */
    itx = htp_tx_get_user_data(tx);

    /* Check for the "end-of-request" indicator. */
    if (txdata->data == NULL) {
        if (tx->request_entity_len == 0) {
            /// @todo Need a way to determine if the request was supposed to
            ///       have body, not if it did have a body.
            ib_tx_mark_nobody(itx);
        }
        ib_state_notify_request_body(ib, itx);
        IB_FTRACE_RET_INT(HTP_OK);
    }

    /* Fill in a temporary ib_txdata_t structure and use it
     * to notify the engine of transaction data.
     */
    itxdata.ib = ib;
    itxdata.mp = itx->mp;
    itxdata.tx = itx;
    itxdata.dtype = IB_DTYPE_HTTP_BODY;
    itxdata.dalloc = txdata->len;
    itxdata.dlen = txdata->len;
    itxdata.data = (uint8_t *)txdata->data;

    ib_state_notify_tx_data_in(ib, &itxdata);

    IB_FTRACE_RET_INT(HTP_OK);
}

static int modhtp_htp_request_trailer(htp_connp_t *connp)
{
    IB_FTRACE_INIT(modhtp_htp_request_trailer);
    modhtp_context_t *modctx = htp_connp_get_user_data(connp);
    htp_tx_t *tx = connp->in_tx;
    ib_conn_t *iconn = modctx->iconn;
    ib_engine_t *ib = iconn->ib;
    ib_tx_t *itx;

    /* Use the current parser transaction to generate fields. */
    ib_log_debug(ib, 9, "LIBHTP: state=%d", connp->in_status);
    if (connp->in_status == STREAM_STATE_ERROR) {
        ib_log_error(ib, 3, "HTP Parser Error");
    }
    if (tx == NULL) {
        /// @todo Set error.
        IB_FTRACE_RET_INT(HTP_ERROR);
    }
    if (tx->flags) {
        ib_log_error(ib, 4, "HTP parser flagged an event: 0x%08x", tx->flags);
        /// @todo Do something about it
    }


    /* Fetch the ironbee transaction and notify the engine
     * that more transaction data has arrived.
     */
    itx = htp_tx_get_user_data(tx);

    /// @todo Notify tx_datain_event w/request trailer
    ib_log_debug(ib, 4, "TODO: tx_datain_event w/request trailer: tx=%p", itx);

    IB_FTRACE_RET_INT(HTP_OK);
}

static int modhtp_htp_request(htp_connp_t *connp)
{
    IB_FTRACE_INIT(modhtp_htp_request);
    modhtp_context_t *modctx = htp_connp_get_user_data(connp);
    htp_tx_t *tx = connp->in_tx;
    ib_conn_t *iconn = modctx->iconn;
    ib_engine_t *ib = iconn->ib;
    ib_tx_t *itx;

    /* Use the current parser transaction to generate fields. */
    ib_log_debug(ib, 9, "LIBHTP: state=%d", connp->in_status);
    if (connp->in_status == STREAM_STATE_ERROR) {
        ib_log_error(ib, 3, "HTP Parser Error");
    }
    if (tx == NULL) {
        /// @todo Set error.
        IB_FTRACE_RET_INT(HTP_ERROR);
    }
    if (tx->flags) {
        ib_log_error(ib, 4, "HTP parser flagged an event: 0x%08x", tx->flags);
        /// @todo Do something about it
    }


    /* Fetch the ironbee transaction, determine if this is a no-body
     * request and notify the engine that the request body is available
     * and is now finished.
     */
    itx = htp_tx_get_user_data(tx);

    ib_state_notify_request_finished(ib, itx);

    IB_FTRACE_RET_INT(HTP_OK);
}

static int modhtp_htp_response_line(htp_connp_t *connp)
{
    IB_FTRACE_INIT(modhtp_htp_response_line);
    modhtp_context_t *modctx = htp_connp_get_user_data(connp);
    htp_tx_t *tx = connp->out_tx;
    ib_conn_t *iconn = modctx->iconn;
    ib_engine_t *ib = iconn->ib;
    ib_txdata_t itxdata;
    ib_tx_t *itx;

    /* Use the current parser transaction to generate fields. */
    ib_log_debug(ib, 9, "LIBHTP: state=%d", connp->out_status);
    if (connp->out_status == STREAM_STATE_ERROR) {
        ib_log_error(ib, 3, "HTP Parser Error");
    }
    if (tx == NULL) {
        /// @todo Set error.
        IB_FTRACE_RET_INT(HTP_ERROR);
    }
    if (tx->flags) {
        ib_log_error(ib, 4, "HTP parser flagged an event: 0x%08x", tx->flags);
        /// @todo Do something about it
    }


    /* Fetch the ironbee transaction and notify the engine
     * that more transaction data has arrived.
     */
    itx = htp_tx_get_user_data(tx);

    ib_state_notify_response_started(ib, itx);

    /* Fill in a temporary ib_txdata_t structure and use it
     * to notify the engine of transaction data.
     */
    itxdata.ib = ib;
    itxdata.mp = itx->mp;
    itxdata.tx = itx;
    itxdata.dtype = IB_DTYPE_HTTP_LINE;
    itxdata.dalloc = bstr_size(tx->response_line_raw);
    itxdata.dlen = bstr_len(tx->response_line_raw);
    itxdata.data = (uint8_t *)bstr_ptr(tx->response_line_raw);

    ib_state_notify_tx_data_out(ib, &itxdata);

    IB_FTRACE_RET_INT(HTP_OK);
}

static int modhtp_htp_response_headers(htp_connp_t *connp)
{
    IB_FTRACE_INIT(modhtp_htp_response_headers);
    modhtp_context_t *modctx = htp_connp_get_user_data(connp);
    htp_tx_t *tx = connp->out_tx;
    htp_header_line_t *hline;
    ib_conn_t *iconn = modctx->iconn;
    ib_engine_t *ib = iconn->ib;
    ib_txdata_t itxdata;
    ib_tx_t *itx;

    /* Use the current parser transaction to generate fields. */
    ib_log_debug(ib, 9, "LIBHTP: state=%d", connp->out_status);
    if (connp->out_status == STREAM_STATE_ERROR) {
        ib_log_error(ib, 3, "HTP Parser Error");
    }
    if (tx == NULL) {
        /// @todo Set error.
        IB_FTRACE_RET_INT(HTP_ERROR);
    }
    if (tx->flags) {
        ib_log_error(ib, 4, "HTP parser flagged an event: 0x%08x", tx->flags);
        /// @todo Do something about it
    }


    /* Fetch the ironbee transaction and notify the engine
     * that the request headers are now available.
     */
    itx = htp_tx_get_user_data(tx);

    /* Fill in a temporary ib_txdata_t structure for each header line
     * and use it to notify the engine of transaction data.
     */
    itxdata.ib = ib;
    itxdata.mp = itx->mp;
    itxdata.tx = itx;
    itxdata.dtype = IB_DTYPE_HTTP_HEADER;
    list_iterator_reset(tx->response_header_lines);
    while ((hline = list_iterator_next(tx->response_header_lines)) != NULL) {
        itxdata.dalloc = bstr_size(hline->line);
        itxdata.dlen = bstr_len(hline->line);
        itxdata.data = (uint8_t *)bstr_ptr(hline->line);

        ib_state_notify_tx_data_out(ib, &itxdata);
    }

    /* Headers separator */
    itxdata.dalloc = bstr_size(tx->response_headers_sep);
    itxdata.dlen = bstr_len(tx->response_headers_sep);
    itxdata.data = (uint8_t *)bstr_ptr(tx->response_headers_sep);
    ib_state_notify_tx_data_out(ib, &itxdata);

    /* The full headers are now available. */
    ib_state_notify_response_headers(ib, itx);

    IB_FTRACE_RET_INT(HTP_OK);
}

static int modhtp_htp_response_body_data(htp_tx_data_t *txdata)
{
    IB_FTRACE_INIT(modhtp_htp_response_body_data);
    htp_connp_t *connp = txdata->tx->connp;
    modhtp_context_t *modctx = htp_connp_get_user_data(connp);
    htp_tx_t *tx = connp->out_tx;
    ib_conn_t *iconn = modctx->iconn;
    ib_engine_t *ib = iconn->ib;
    ib_txdata_t itxdata;
    ib_tx_t *itx;

    /* Use the current parser transaction to generate fields. */
    ib_log_debug(ib, 9, "LIBHTP: state=%d", connp->out_status);
    if (connp->out_status == STREAM_STATE_ERROR) {
        ib_log_error(ib, 3, "HTP Parser Error");
    }
    if (tx == NULL) {
        /// @todo Set error.
        IB_FTRACE_RET_INT(HTP_ERROR);
    }
    if (tx->flags) {
        ib_log_error(ib, 4, "HTP parser flagged an event: 0x%08x", tx->flags);
        /// @todo Do something about it
    }


    /* Fetch the ironbee transaction and notify the engine
     * that more transaction data has arrived.
     */
    itx = htp_tx_get_user_data(tx);

    /* Check for the "end-of-response" indicator. */
    if (txdata->data == NULL) {
        ib_state_notify_response_body(ib, itx);
        IB_FTRACE_RET_INT(HTP_OK);
    }

    /* Fill in a temporary ib_txdata_t structure and use it
     * to notify the engine of transaction data.
     */
    itxdata.ib = ib;
    itxdata.mp = itx->mp;
    itxdata.tx = itx;
    itxdata.dtype = IB_DTYPE_HTTP_BODY;
    itxdata.dalloc = txdata->len;
    itxdata.dlen = txdata->len;
    itxdata.data = (uint8_t *)txdata->data;

    ib_state_notify_tx_data_out(ib, &itxdata);

    IB_FTRACE_RET_INT(HTP_OK);
}

static int modhtp_htp_response(htp_connp_t *connp)
{
    IB_FTRACE_INIT(modhtp_htp_response);
    modhtp_context_t *modctx = htp_connp_get_user_data(connp);
    htp_tx_t *tx = connp->out_tx;
    ib_conn_t *iconn = modctx->iconn;
    ib_engine_t *ib = iconn->ib;
    ib_tx_t *itx;

    /* Use the current parser transaction to generate fields. */
    ib_log_debug(ib, 9, "LIBHTP: state=%d", connp->out_status);
    if (connp->out_status == STREAM_STATE_ERROR) {
        ib_log_error(ib, 3, "HTP Parser Error");
    }
    if (tx == NULL) {
        /// @todo Set error.
        IB_FTRACE_RET_INT(HTP_ERROR);
    }
    if (tx->flags) {
        ib_log_error(ib, 4, "HTP parser flagged an event: 0x%08x", tx->flags);
        /// @todo Do something about it
    }


    /* Fetch the ironbee transaction and notify the engine
     * that the response body is available, the response
     * is finished and logging has begun.
     */
    itx = htp_tx_get_user_data(tx);

    ib_state_notify_response_finished(ib, itx);

    /* Destroy the transaction. */
    /// @todo Perhaps the engine should do this instead via an event???
    ib_log_debug(ib, 9, "Destroying transaction structure");
    ib_tx_destroy(itx);
    htp_tx_destroy(tx);

    IB_FTRACE_RET_INT(HTP_OK);
}

static int modhtp_htp_response_trailer(htp_connp_t *connp)
{
    IB_FTRACE_INIT(modhtp_htp_response_trailer);
    modhtp_context_t *modctx = htp_connp_get_user_data(connp);
    htp_tx_t *tx = connp->out_tx;
    ib_conn_t *iconn = modctx->iconn;
    ib_engine_t *ib = iconn->ib;
    ib_tx_t *itx;

    /* Use the current parser transaction to generate fields. */
    /// @todo Check htp state, etc.
    ib_log_debug(ib, 9, "LIBHTP: state=%d", connp->out_status);
    if (connp->out_status == STREAM_STATE_ERROR) {
        ib_log_error(ib, 3, "HTP Parser Error");
    }
    if (tx == NULL) {
        /// @todo Set error.
        IB_FTRACE_RET_INT(HTP_ERROR);
    }
    if (tx->flags) {
        ib_log_error(ib, 4, "HTP parser flagged an event: 0x%08x", tx->flags);
        /// @todo Do something about it
    }


    /* Fetch the ironbee transaction and notify the engine
     * that more transaction data has arrived.
     */
    itx = htp_tx_get_user_data(tx);

    /// @todo Notify tx_dataout_event w/response trailer
    ib_log_debug(ib, 4, "TODO: tx_dataout_event w/response trailer: tx=%p", itx);

    IB_FTRACE_RET_INT(HTP_OK);
}

/* -- Provider Interface Implementation -- */

static ib_status_t modhtp_iface_init(ib_provider_inst_t *pi,
                                     ib_conn_t *iconn)
{
    IB_FTRACE_INIT(modhtp_iface_init);
    ib_engine_t *ib = iconn->ib;
    ib_context_t *ctx = iconn->ctx;
    modhtp_cfg_t *modcfg;
    modhtp_context_t *modctx;
    ib_status_t rc;
    int personality;

    /* Get the module config. */
    rc = ib_context_module_config(ctx, IB_MODULE_STRUCT_PTR, (void *)&modcfg);
    if (rc != IB_OK) {
        ib_log_error(ib, 0, "Failed to fetch module %s config: %d",
                     MODULE_NAME_STR, rc);
        IB_FTRACE_RET_STATUS(rc);
    }

    ib_log_debug(ib, 9, "Creating LibHTP parser");

    /* Create a context. */
    modctx = ib_mpool_calloc(iconn->mp, 1, sizeof(*modctx));
    if (modctx == NULL) {
        IB_FTRACE_RET_STATUS(IB_EALLOC);
    }

    /* Figure out the personality to use. */
    personality = modhtp_personality(modcfg->personality);
    if (personality == -1) {
        personality = HTP_SERVER_APACHE_2_2;
    }

    /* Configure parser. */
    modctx->htp_cfg = htp_config_create();
    if (modctx->htp_cfg == NULL) {
        IB_FTRACE_RET_STATUS(IB_EALLOC);
    }
    htp_config_set_server_personality(modctx->htp_cfg, personality);
    /// @todo Make all these configurable???
    modctx->htp_cfg->log_level = HTP_LOG_DEBUG2;
    htp_config_set_tx_auto_destroy(modctx->htp_cfg, 0);
    htp_config_set_generate_request_uri_normalized(modctx->htp_cfg, 1);

    htp_config_register_urlencoded_parser(modctx->htp_cfg);
    htp_config_register_multipart_parser(modctx->htp_cfg);
    htp_config_register_log(modctx->htp_cfg, modhtp_callback_log);

    /* Setup context and create the parser. */
    modctx->ib = ib;
    modctx->iconn = iconn;
    modctx->modcfg = modcfg;
    modctx->htp = htp_connp_create(modctx->htp_cfg);
    if (modctx->htp == NULL) {
        IB_FTRACE_RET_STATUS(IB_EALLOC);
    }

    /* Open the connection */
    htp_connp_open(modctx->htp,
                   iconn->remote_ipstr, iconn->remote_port,
                   iconn->local_ipstr, iconn->local_port,
                   (htp_time_t *)&iconn->started);


    /* Store the context. */
    rc = ib_hash_set(iconn->data, "MODHTP_CTX", modctx);
    if (rc != IB_OK) {
        IB_FTRACE_RET_STATUS(rc);
    }
    htp_connp_set_user_data(modctx->htp, modctx);

    /* Register callbacks. */
    htp_config_register_transaction_start(modctx->htp_cfg,
                                          modhtp_htp_tx_start);
    htp_config_register_request_line(modctx->htp_cfg,
                                     modhtp_htp_request_line);
    htp_config_register_request_headers(modctx->htp_cfg,
                                        modhtp_htp_request_headers);
    htp_config_register_request_body_data(modctx->htp_cfg,
                                          modhtp_htp_request_body_data);
    htp_config_register_request_trailer(modctx->htp_cfg,
                                        modhtp_htp_request_trailer);
    htp_config_register_request(modctx->htp_cfg,
                                modhtp_htp_request);
    htp_config_register_response_line(modctx->htp_cfg,
                                      modhtp_htp_response_line);
    htp_config_register_response_headers(modctx->htp_cfg,
                                         modhtp_htp_response_headers);
    htp_config_register_response_body_data(modctx->htp_cfg,
                                           modhtp_htp_response_body_data);
    htp_config_register_response_trailer(modctx->htp_cfg,
                                         modhtp_htp_response_trailer);
    htp_config_register_response(modctx->htp_cfg,
                                 modhtp_htp_response);

    IB_FTRACE_RET_STATUS(IB_OK);
}

static ib_status_t modhtp_iface_disconnect(ib_provider_inst_t *pi,
                                           ib_conn_t *iconn)
{
    IB_FTRACE_INIT(modhtp_iface_disconnect);
    ib_engine_t *ib = iconn->ib;
    modhtp_context_t *modctx;
    ib_status_t rc;

    /* Fetch context from the connection. */
    /// @todo Move this into a ib_conn_t field
    rc = ib_hash_get(iconn->data, "MODHTP_CTX", (void *)&modctx);
    if (rc != IB_OK) {
        IB_FTRACE_RET_STATUS(rc);
    }

    ib_log_debug(ib, 9, "Destroying LibHTP parser");

    /* Destroy the parser on disconnect. */
    htp_connp_destroy_all(modctx->htp);

    IB_FTRACE_RET_STATUS(IB_OK);
}

static ib_status_t modhtp_iface_data_in(ib_provider_inst_t *pi,
                                        ib_conndata_t *qcdata)
{
    IB_FTRACE_INIT(modhtp_iface_data_in);
    ib_engine_t *ib = qcdata->ib;
    ib_conn_t *iconn = qcdata->conn;
    modhtp_context_t *modctx;
    htp_connp_t *htp;
    ib_status_t rc;
    struct timeval tv;
    int ec;

    gettimeofday(&tv, NULL);

    /* Fetch context from the connection. */
    /// @todo Move this into a ib_conn_t field
    rc = ib_hash_get(iconn->data, "MODHTP_CTX", (void *)&modctx);
    if (rc != IB_OK) {
        IB_FTRACE_RET_STATUS(rc);
    }
    htp = modctx->htp;

    ib_log_debug(ib, 9, "LibHTP incoming data status=%d", htp->in_status);
    ib_log_debug(ib, 9,
                 "DATA: %s:%d -> %s:%d len=%d %" IB_BYTESTR_FMT,
                 iconn->remote_ipstr, iconn->remote_port,
                 iconn->local_ipstr, iconn->local_port,
                 (int)qcdata->dlen,
                 IB_BYTESTRSL_FMT_PARAM(qcdata->data, qcdata->dlen));

    switch(htp->in_status) {
        case STREAM_STATE_NEW:
        case STREAM_STATE_OPEN:
        case STREAM_STATE_DATA:
            /* Let the parser see the data. */
            ec = htp_connp_req_data(htp, &tv, qcdata->data, qcdata->dlen);
            if (ec == STREAM_STATE_DATA_OTHER) {
                ib_log_error(ib, 4, "LibHTP parser blocked: %d", ec);
                /// @todo Buffer it for next time?
            }
            else if (ec != STREAM_STATE_DATA) {
                ib_log_error(ib, 4, "LibHTP request parsing error: %d", ec);
            }
            break;
        case STREAM_STATE_ERROR:
            ib_log_error(ib, 4, "LibHTP parser in \"error\" state");
            break;
        case STREAM_STATE_DATA_OTHER:
            ib_log_error(ib, 4, "LibHTP parser in \"other\" state");
            break;
        default:
            ib_log_error(ib, 4, "LibHTP parser in unhandled state %d",
                         htp->in_status);
    }

    IB_FTRACE_RET_STATUS(IB_OK);
}

static ib_status_t modhtp_iface_data_out(ib_provider_inst_t *pi,
                                         ib_conndata_t *qcdata)
{
    IB_FTRACE_INIT(modhtp_iface_data_out);
    ib_engine_t *ib = qcdata->ib;
    ib_conn_t *iconn = qcdata->conn;
    modhtp_context_t *modctx;
    htp_connp_t *htp;
    ib_status_t rc;
    struct timeval tv;
    int ec;

    gettimeofday(&tv, NULL);

    /* Fetch context from the connection. */
    /// @todo Move this into a ib_conn_t field
    rc = ib_hash_get(iconn->data, "MODHTP_CTX", (void *)&modctx);
    if (rc != IB_OK) {
        IB_FTRACE_RET_STATUS(rc);
    }
    htp = modctx->htp;

    ib_log_debug(ib, 9, "LibHTP outgoing data status=%d", htp->out_status);
    ib_log_debug(ib, 9,
                 "DATA: %s:%d -> %s:%d len=%d %" IB_BYTESTR_FMT,
                 iconn->local_ipstr, iconn->local_port,
                 iconn->remote_ipstr, iconn->remote_port,
                 (int)qcdata->dlen,
                 IB_BYTESTRSL_FMT_PARAM(qcdata->data, qcdata->dlen));

    switch(htp->out_status) {
        case STREAM_STATE_NEW:
        case STREAM_STATE_OPEN:
        case STREAM_STATE_DATA:
            /* Let the parser see the data. */
            ec = htp_connp_res_data(htp, &tv, qcdata->data, qcdata->dlen);
            if (ec == STREAM_STATE_DATA_OTHER) {
                ib_log_error(ib, 4, "LibHTP parser blocked: %d", ec);
                /// @todo Buffer it for next time?
            }
            else if (ec != STREAM_STATE_DATA) {
                ib_log_error(ib, 4, "LibHTP response parsing error: %d", ec);
            }
            break;
        case STREAM_STATE_ERROR:
            ib_log_error(ib, 4, "LibHTP parser in \"error\" state");
            break;
        case STREAM_STATE_DATA_OTHER:
            ib_log_error(ib, 4, "LibHTP parser in \"other\" state");
            break;
        default:
            ib_log_error(ib, 4, "LibHTP parser in unhandled state %d",
                         htp->out_status);
    }

    IB_FTRACE_RET_STATUS(IB_OK);
}

static ib_status_t modhtp_iface_gen_request_header_fields(ib_provider_inst_t *pi,
                                                          ib_tx_t *itx)
{
    IB_FTRACE_INIT(modhtp_iface_gen_request_header_fields);
    ib_engine_t *ib = itx->ib;
    ib_context_t *ctx = itx->ctx;
    ib_conn_t *iconn = itx->conn;
    ib_field_t *f;
    modhtp_cfg_t *modcfg;
    modhtp_context_t *modctx;
    htp_tx_t *tx;
    ib_status_t rc;

    /* Get the module config. */
    rc = ib_context_module_config(ctx, IB_MODULE_STRUCT_PTR, (void *)&modcfg);
    if (rc != IB_OK) {
        ib_log_error(ib, 0, "Failed to fetch module %s config: %d",
                     MODULE_NAME_STR, rc);
        IB_FTRACE_RET_STATUS(rc);
    }

    /* Fetch context from the connection. */
    /// @todo Move this into a ib_conn_t field
    rc = ib_hash_get(iconn->data, "MODHTP_CTX", (void *)&modctx);
    if (rc != IB_OK) {
        IB_FTRACE_RET_STATUS(rc);
    }

    /* Use the current parser transaction to generate fields. */
    /// @todo Check htp state, etc.
    tx = modctx->htp->in_tx;
    if (tx != NULL) {
        htp_tx_set_user_data(tx, itx);

        modhtp_field_gen_bytestr(itx->dpi,
                                 "request_line",
                                 tx->request_line,
                                 NULL);

        modhtp_field_gen_bytestr(itx->dpi,
                                 "request_method",
                                 tx->request_method,
                                 NULL);

        modhtp_field_gen_bytestr(itx->dpi,
                                 "request_protocol",
                                 tx->request_protocol,
                                 NULL);

        modhtp_field_gen_bytestr(itx->dpi,
                                 "request_uri",
                                 tx->request_uri_normalized,
                                 NULL);

        modhtp_field_gen_bytestr(itx->dpi,
                                 "request_uri_raw",
                                 tx->request_uri,
                                 NULL);

        modhtp_field_gen_bytestr(itx->dpi,
                                 "request_uri_scheme",
                                 tx->parsed_uri->scheme,
                                 NULL);

        modhtp_field_gen_bytestr(itx->dpi,
                                 "request_uri_username",
                                 tx->parsed_uri->username,
                                 NULL);

        modhtp_field_gen_bytestr(itx->dpi,
                                 "request_uri_password",
                                 tx->parsed_uri->password,
                                 NULL);

        modhtp_field_gen_bytestr(itx->dpi,
                                 "request_uri_host",
                                 tx->parsed_uri->hostname,
                                 NULL);

        modhtp_field_gen_bytestr(itx->dpi,
                                 "request_host",
                                 tx->parsed_uri->hostname,
                                 NULL);

        modhtp_field_gen_bytestr(itx->dpi,
                                 "request_uri_port",
                                 tx->parsed_uri->port,
                                 NULL);

        modhtp_field_gen_bytestr(itx->dpi,
                                 "request_uri_path",
                                 tx->parsed_uri->path,
                                 NULL);

        modhtp_field_gen_bytestr(itx->dpi,
                                 "request_uri_query",
                                 tx->parsed_uri->query,
                                 NULL);

        modhtp_field_gen_bytestr(itx->dpi,
                                 "request_uri_fragment",
                                 tx->parsed_uri->fragment,
                                 NULL);

        rc = ib_data_add_list(itx->dpi, "request_headers", &f);
        if (rc == IB_OK) {
            bstr *key = NULL;
            htp_header_t *h = NULL;

            /// @todo Make this a function
            table_iterator_reset(tx->request_headers);
            ib_log_debug(ib, 4, "Adding request_headers fields");
            while ((key = table_iterator_next(tx->request_headers,
                                              (void *)&h)) != NULL)
            {
                ib_field_t *lf;

                /* Create a list field as an alias into htp memory. */
                rc = ib_field_alias_mem_ex(&lf,
                                           itx->mp,
                                           bstr_ptr(h->name),
                                           bstr_len(h->name),
                                           (uint8_t *)bstr_ptr(h->value),
                                           bstr_len(h->value));
                if (rc != IB_OK) {
                    ib_log_debug(ib, 9, "Failed to create field: %d", rc);
                }

                /* Add the field to the field list. */
                rc = ib_field_list_add(f, lf);
                if (rc != IB_OK) {
                    ib_log_debug(ib, 9, "Failed to add field: %d", rc);
                }
            }
        }
        else {
            ib_log_error(ib, 4, "Failed to create request headers list: %d", rc);
        }

        rc = ib_data_add_list(itx->dpi, "request_uri_params", &f);
        if (tx->request_params_query && rc == IB_OK) {
            bstr *key = NULL;
            bstr *value = NULL;

            /// @todo Make this a function
            table_iterator_reset(tx->request_params_query);
            ib_log_debug(ib, 4, "Adding request_params_query fields");
            while ((key = table_iterator_next(tx->request_params_query,
                                              (void *)&value)) != NULL)
            {
                ib_field_t *lf;

                /* Create a list field as an alias into htp memory. */
                rc = ib_field_alias_mem_ex(&lf,
                                           itx->mp,
                                           bstr_ptr(key),
                                           bstr_len(key),
                                           (uint8_t *)bstr_ptr(value),
                                           bstr_len(value));
                if (rc != IB_OK) {
                    ib_log_debug(ib, 9, "Failed to create field: %d", rc);
                }

                /* Add the field to the field list. */
                rc = ib_field_list_add(f, lf);
                if (rc != IB_OK) {
                    ib_log_debug(ib, 9, "Failed to add field: %d", rc);
                }
            }
        }
        else {
            ib_log_error(ib, 4, "Failed to create request uri parameters: %d", rc);
        }
    }

    IB_FTRACE_RET_STATUS(IB_OK);
}

static ib_status_t modhtp_iface_gen_response_header_fields(ib_provider_inst_t *pi,
                                                           ib_tx_t *itx)
{
    IB_FTRACE_INIT(modhtp_iface_gen_response_header_fields);
    ib_engine_t *ib = itx->ib;
    ib_context_t *ctx = itx->ctx;
    ib_conn_t *iconn = itx->conn;
    ib_field_t *f;
    modhtp_cfg_t *modcfg;
    modhtp_context_t *modctx;
    htp_tx_t *tx;
    ib_status_t rc;

    /* Get the module config. */
    rc = ib_context_module_config(ctx, IB_MODULE_STRUCT_PTR, (void *)&modcfg);
    if (rc != IB_OK) {
        ib_log_error(ib, 0, "Failed to fetch module %s config: %d",
                     MODULE_NAME_STR, rc);
        IB_FTRACE_RET_STATUS(rc);
    }

    /* Fetch context from the connection. */
    /// @todo Move this into a ib_conn_t field
    rc = ib_hash_get(iconn->data, "MODHTP_CTX", (void *)&modctx);
    if (rc != IB_OK) {
        ib_log_error(ib, 0, "Failed to fetch module %s context: %d",
                     MODULE_NAME_STR, rc);
        IB_FTRACE_RET_STATUS(rc);
    }
    

    /* Use the current parser transaction to generate fields. */
    /// @todo Check htp state, etc.
    tx = modctx->htp->out_tx;
    if (tx != NULL) {
        modhtp_field_gen_bytestr(itx->dpi,
                                 "response_line",
                                 tx->response_line,
                                 NULL);

        modhtp_field_gen_bytestr(itx->dpi,
                                 "response_protocol",
                                 tx->response_protocol,
                                 NULL);

        modhtp_field_gen_bytestr(itx->dpi,
                                 "response_status",
                                 tx->response_status,
                                 NULL);

        modhtp_field_gen_bytestr(itx->dpi,
                                 "response_message",
                                 tx->response_message,
                                 NULL);

        /// @todo Need a table type that can have more than one
        ///       of the same header.
        rc = ib_data_add_list(itx->dpi, "response_headers", &f);
        if (rc == IB_OK) {
            bstr *key = NULL;
            htp_header_t *h = NULL;

            /// @todo Make this a function
            table_iterator_reset(tx->response_headers);
            while ((key = table_iterator_next(tx->response_headers,
                                              (void *)&h)) != NULL)
            {
                ib_field_t *lf;

                /* Create a list field as an alias into htp memory. */
                rc = ib_field_alias_mem_ex(&lf,
                                           itx->mp,
                                           bstr_ptr(h->name),
                                           bstr_len(h->name),
                                           (uint8_t *)bstr_ptr(h->value),
                                           bstr_len(h->value));
                if (rc != IB_OK) {
                    ib_log_debug(ib, 9, "Failed to create field: %d", rc);
                }

                /* Add the field to the field list. */
                rc = ib_field_list_add(f, lf);
                if (rc != IB_OK) {
                    ib_log_debug(ib, 9, "Failed to add field: %d", rc);
                }
            }
        }
        else {
            ib_log_error(ib, 4, "Failed to create response headers list: %d", rc);
        }
    }

    IB_FTRACE_RET_STATUS(IB_OK);
}

static IB_PROVIDER_IFACE_TYPE(parser) modhtp_parser_iface = {
    IB_PROVIDER_IFACE_HEADER_DEFAULTS,

    /* Optional Parser Functions */
    modhtp_iface_init,
    NULL,
    modhtp_iface_disconnect,

    /* Required Parser Functions */
    modhtp_iface_data_in,
    modhtp_iface_data_out,
    modhtp_iface_gen_request_header_fields,
    modhtp_iface_gen_response_header_fields
};


/* -- Module Routines -- */

static ib_status_t modhtp_init(ib_engine_t *ib,
                               ib_module_t *m)
{
    IB_FTRACE_INIT(modhtp_init);
    ib_status_t rc;

    /* Register as a parser provider. */
    rc = ib_provider_register(ib, IB_PROVIDER_TYPE_PARSER,
                              MODULE_NAME_STR, NULL,
                              &modhtp_parser_iface,
                              NULL);
    if (rc != IB_OK) {
        ib_log_error(ib, 3,
                     MODULE_NAME_STR ": Error registering htp parser provider: "
                     "%d", rc);
        IB_FTRACE_RET_STATUS(IB_OK);
    }

    IB_FTRACE_RET_STATUS(IB_OK);
}

static ib_status_t modhtp_fini(ib_engine_t *ib,
                               ib_module_t *m)
{
    IB_FTRACE_INIT(modhtp_fini);
    /// @todo Nothing yet???

    IB_FTRACE_RET_STATUS(IB_OK);
}

static ib_status_t modhtp_context_init(ib_engine_t *ib,
                                       ib_module_t *m,
                                       ib_context_t *ctx)
{
    IB_FTRACE_INIT(modhtp_context_init);
    IB_FTRACE_RET_STATUS(IB_OK);
}

static ib_status_t modhtp_context_fini(ib_engine_t *ib,
                                       ib_module_t *m,
                                       ib_context_t *ctx)
{
    IB_FTRACE_INIT(modhtp_context_fini);
    IB_FTRACE_RET_STATUS(IB_OK);
}

static IB_CFGMAP_INIT_STRUCTURE(modhtp_config_map) = {
    IB_CFGMAP_INIT_ENTRY(
        MODULE_NAME_STR ".personality",
        IB_FTYPE_NULSTR,
        &modhtp_global_cfg,
        personality,
        "Apache_2_2"
    ),
    IB_CFGMAP_INIT_LAST
};

/**
 * Module structure.
 *
 * This structure defines some metadata, config data and various functions.
 */
IB_MODULE_INIT(
    IB_MODULE_HEADER_DEFAULTS,           /**< Default metadata */
    MODULE_NAME_STR,                     /**< Module name */
    IB_MODULE_CONFIG(&modhtp_global_cfg),/**< Global config data */
    modhtp_config_map,                   /**< Configuration field map */
    NULL,                                /**< Config directive map */
    modhtp_init,                         /**< Initialize function */
    modhtp_fini,                         /**< Finish function */
    modhtp_context_init,                 /**< Context init function */
    modhtp_context_fini                  /**< Context fini function */
);

