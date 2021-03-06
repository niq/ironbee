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
 * @brief IronBee - Data Access
 *
 * @author Brian Rectanus <brectanus@qualys.com>
 */

#include "ironbee_config_auto.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>


#include <ironbee/engine.h>
#include <ironbee/util.h>
#include <ironbee/provider.h>

#include "ironbee_private.h"


/* -- Exported Data Access Routines -- */

ib_status_t ib_data_add(ib_provider_inst_t *dpi,
                        ib_field_t *f)
{
    IB_FTRACE_INIT(ib_data_add);
    IB_PROVIDER_API_TYPE(data) *api =
        (IB_PROVIDER_API_TYPE(data) *)dpi->pr->api;
    ib_status_t rc;

    rc = api->add(dpi, f, f->name, f->nlen);
    IB_FTRACE_RET_STATUS(rc);
}

ib_status_t ib_data_add_named(ib_provider_inst_t *dpi,
                              ib_field_t *f,
                              const char *key,
                              size_t klen)
{
    IB_FTRACE_INIT(ib_data_add);
    IB_PROVIDER_API_TYPE(data) *api =
        (IB_PROVIDER_API_TYPE(data) *)dpi->pr->api;
    ib_status_t rc;

    rc = api->add(dpi, f, key, klen);
    IB_FTRACE_RET_STATUS(rc);
}

ib_status_t ib_data_add_num_ex(ib_provider_inst_t *dpi,
                               const char *name,
                               size_t nlen,
                               ib_num_t val,
                               ib_field_t **pf)
{
    IB_FTRACE_INIT(ib_data_add_num_ex);
    IB_PROVIDER_API_TYPE(data) *api =
        (IB_PROVIDER_API_TYPE(data) *)dpi->pr->api;
    ib_field_t *f;
    ib_status_t rc;

    if (pf != NULL) {
        *pf = NULL;
    }

    rc = ib_field_create_ex(&f, dpi->mp, name, nlen, IB_FTYPE_NUM, &val);
    if (rc != IB_OK) {
        IB_FTRACE_RET_STATUS(rc);
    }

    rc = api->add(dpi, f, f->name, f->nlen);
    if ((rc == IB_OK) && (pf != NULL)) {
        *pf = f;
    }

    IB_FTRACE_RET_STATUS(rc);
}

ib_status_t ib_data_add_nulstr_ex(ib_provider_inst_t *dpi,
                                  const char *name,
                                  size_t nlen,
                                  char *val,
                                  ib_field_t **pf)
{
    IB_FTRACE_INIT(ib_data_add_nulstr_ex);
    IB_PROVIDER_API_TYPE(data) *api =
        (IB_PROVIDER_API_TYPE(data) *)dpi->pr->api;
    ib_field_t *f;
    ib_status_t rc;

    if (pf != NULL) {
        *pf = NULL;
    }

    rc = ib_field_create_ex(&f, dpi->mp, name, nlen, IB_FTYPE_NULSTR, &val);
    if (rc != IB_OK) {
        IB_FTRACE_RET_STATUS(rc);
    }

    rc = api->add(dpi, f, f->name, f->nlen);
    if ((rc == IB_OK) && (pf != NULL)) {
        *pf = f;
    }

    IB_FTRACE_RET_STATUS(rc);
}

ib_status_t ib_data_add_bytestr_ex(ib_provider_inst_t *dpi,
                                   const char *name,
                                   size_t nlen,
                                   uint8_t *val,
                                   size_t vlen,
                                   ib_field_t **pf)
{
    IB_FTRACE_INIT(ib_data_add_bytestr_ex);
    IB_PROVIDER_API_TYPE(data) *api =
        (IB_PROVIDER_API_TYPE(data) *)dpi->pr->api;
    ib_field_t *f;
    ib_status_t rc;

    if (pf != NULL) {
        *pf = NULL;
    }

    rc = ib_field_alias_mem_ex(&f, dpi->mp, name, nlen, val, vlen);
    if (rc != IB_OK) {
        IB_FTRACE_RET_STATUS(rc);
    }

    rc = api->add(dpi, f, f->name, f->nlen);
    if ((rc == IB_OK) && (pf != NULL)) {
        *pf = f;
    }

    IB_FTRACE_RET_STATUS(rc);
}

ib_status_t ib_data_add_list_ex(ib_provider_inst_t *dpi,
                                const char *name,
                                size_t nlen,
                                ib_field_t **pf)
{
    IB_FTRACE_INIT(ib_data_add_list_ex);
    IB_PROVIDER_API_TYPE(data) *api =
        (IB_PROVIDER_API_TYPE(data) *)dpi->pr->api;
    ib_field_t *f;
    ib_status_t rc;

    if (pf != NULL) {
        *pf = NULL;
    }

    rc = ib_field_create_ex(&f, dpi->mp, IB_S2SL(name), IB_FTYPE_LIST, NULL);
    if (rc != IB_OK) {
        IB_FTRACE_RET_STATUS(rc);
    }

    rc = api->add(dpi, f, f->name, f->nlen);
    if ((rc == IB_OK) && (pf != NULL)) {
        *pf = f;
    }

    IB_FTRACE_RET_STATUS(rc);
}

ib_status_t ib_data_get_ex(ib_provider_inst_t *dpi,
                           const char *name,
                           size_t nlen,
                           ib_field_t **pf)
{
    IB_FTRACE_INIT(ib_data_get_ex);
    IB_PROVIDER_API_TYPE(data) *api =
        (IB_PROVIDER_API_TYPE(data) *)dpi->pr->api;
    ib_status_t rc;

    rc = api->get(dpi, name, nlen, pf);
    IB_FTRACE_RET_STATUS(rc);
}

ib_status_t ib_data_get_all(ib_provider_inst_t *dpi,
                            ib_list_t *list)
{
    IB_FTRACE_INIT(ib_data_get_all);
    IB_PROVIDER_API_TYPE(data) *api =
        (IB_PROVIDER_API_TYPE(data) *)dpi->pr->api;
    ib_status_t rc;

    rc = api->get_all(dpi, list);
    IB_FTRACE_RET_STATUS(rc);
}

ib_status_t ib_data_tfn_get_ex(ib_provider_inst_t *dpi,
                               const char *name,
                               size_t nlen,
                               ib_field_t **pf,
                               const char *tfn)
{
    IB_FTRACE_INIT(ib_data_tfn_get_ex);
    IB_PROVIDER_API_TYPE(data) *api =
        (IB_PROVIDER_API_TYPE(data) *)dpi->pr->api;
    ib_engine_t *ib = dpi->pr->ib;
    char *fullname;
    size_t fnlen;
    size_t tlen;
    size_t i;
    ib_status_t rc;

    /* No tfn just means a normal get. */
    if (tfn == NULL) {
        rc = ib_data_get_ex(dpi, name, nlen, pf);
        IB_FTRACE_RET_STATUS(rc);
    }

    /* Build the full name with tfn: "name.t(tfn)" */
    tlen = strlen(tfn);
    fnlen = nlen + tlen + 4; /* Additional ".t()" bytes */
    fullname = (char *)ib_mpool_alloc(dpi->mp, fnlen);
    memcpy(fullname, name, nlen);
    memcpy(fullname + nlen, ".t(", fnlen - nlen);
    memcpy(fullname + nlen + 3, tfn, fnlen - nlen - 3);
    fullname[fnlen - 1] = ')';

    /* See if there is already a transformed version, otherwise
     * one needs to be created.
     */
    rc = api->get(dpi, fullname, fnlen, pf);
    if (rc == IB_ENOENT) {
        const char *tname;

        /* Get the non-tfn field. */
        rc = api->get(dpi, name, nlen, pf);
        if (rc != IB_OK) {
            ib_log_debug(ib, 4, "Failed to fetch field: %p (%d)", *pf, rc);
            IB_FTRACE_RET_STATUS(rc);
        }

        /* Currently this only works for string type fields. */
        if (   ((*pf)->type != IB_FTYPE_NULSTR)
            && ((*pf)->type != IB_FTYPE_BYTESTR))
        {
            ib_log_error(ib, 4,
                         "Cannot transform a non-string based field type=%d",
                         (int)(*pf)->type);
            IB_FTRACE_RET_STATUS(IB_EINVAL);
        }


        /* Copy the field, noting the tfn. */
        rc = ib_field_copy_ex(pf, dpi->mp, fullname, fnlen, *pf);
        if (rc != IB_OK) {
            IB_FTRACE_RET_STATUS(rc);
        }
        (*pf)->tfn = (char *)ib_mpool_memdup(dpi->mp, tfn, tlen + 1);


        /* Transform. */
        tname = tfn;
        for (i = 0; i <= tlen; i++) {
            ib_tfn_t *t;
            ib_flags_t flags;
            size_t len;

            if ((tfn[i] == ',') || (i == tlen)) {
                len = (tfn + i) - tname;
                rc = ib_tfn_lookup_ex(ib, tname, len, &t);
                if (rc == IB_OK) {
                    ib_log_debug(ib, 7,
                                 "TFN: %" IB_BYTESTR_FMT ".%" IB_BYTESTR_FMT,
                                 IB_BYTESTRSL_FMT_PARAM(name, nlen),
                                 IB_BYTESTRSL_FMT_PARAM(tname, len));

                    rc = ib_tfn_transform_field(t, *pf, &flags);
                    if (rc != IB_OK) {
                        /// @todo What to do here?  Fail or ignore?
                        ib_log_error(ib, 3,
                                     "Transformation failed: %" IB_BYTESTR_FMT,
                                     IB_BYTESTRSL_FMT_PARAM(tname, len));
                    }
                }
                else {
                    /// @todo What to do here?  Fail or ignore?
                    ib_log_error(ib, 4,
                                 "Unknown transformation: %" IB_BYTESTR_FMT,
                                 IB_BYTESTRSL_FMT_PARAM(tname, len));
                }
                tname = tfn + i + 1;

            }
        }

        /* Store the transformed field. */
        rc = ib_data_add_named(dpi, *pf, fullname, fnlen);
        if (rc != IB_OK) {
            ib_log_error(ib, 4,
                         "Cannot store field \"%.*s\" type=%d: %d",
                         (int)fnlen, fullname,
                         (int)(*pf)->type,
                         rc);

            IB_FTRACE_RET_STATUS(rc);
        }
    }

    IB_FTRACE_RET_STATUS(rc);
}

