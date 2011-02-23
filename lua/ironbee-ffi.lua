-- =========================================================================
-- =========================================================================
-- Licensed to Qualys, Inc. (QUALYS) under one or more
-- contributor license agreements.  See the NOTICE file distributed with
-- this work for additional information regarding copyright ownership.
-- QUALYS licenses this file to You under the Apache License, Version 2.0
-- (the "License"); you may not use this file except in compliance with
-- the License.  You may obtain a copy of the License at
--
--     http://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.
-- =========================================================================
-- =========================================================================
--
-- This module allows accessing the IronBee API via luajit FFI. It is
-- loaded through ironbee, and should not be used directly.
--
-- Author: Brian Rectanus <brectanus@qualys.com>
-- =========================================================================

local base = _G
local modules = package.loaded
local ffi = require("ffi")
local io = require("io")
local string = require("string")

module("ironbee-ffi")

-- Mark this lib as preloaded
base.package.preload["ironbee-ffi"] = _M

-- TODO: remove this need to register with engine
base["ironbee-module"] = _M

-- ===============================================
-- Setup some module metadata.
-- ===============================================
_COPYRIGHT = "Copyright (C) 2010-2011 Qualys, Inc."
_DESCRIPTION = "IronBee API via luajit FFI"
_VERSION = "0.1"

-- ===============================================
-- Setup the IronBee C definitions.
-- ===============================================
ffi.cdef[[
    /* Util Types */
    typedef struct ib_mpool_t ib_mpool_t;
    typedef struct ib_dso_t ib_dso_t;
    typedef void ib_dso_sym_t;
    typedef struct ib_hash_t ib_hash_t;
    typedef uint32_t ib_ftype_t;
    typedef uint32_t ib_flags_t;
    typedef uint64_t ib_flags64_t;
    typedef struct ib_cfgmap_t ib_cfgmap_t;
    typedef struct ib_cfgmap_init_t ib_cfgmap_init_t;
    typedef struct ib_field_t ib_field_t;
    typedef struct ib_field_val_t ib_field_val_t;
    typedef struct ib_bytestr_t ib_bytestr_t;
    typedef enum {
        IB_OK,
        IB_DECLINED,
        IB_EUNKNOWN,
        IB_ENOTIMPL,
        IB_EINCOMPAT,
        IB_EALLOC,
        IB_EINVAL,
        IB_ENOENT,
        IB_ETIMEDOUT,
    } ib_status_t;
    typedef enum {
        IB_FTYPE_GENERIC,
        IB_FTYPE_NUM,
        IB_FTYPE_NULSTR,
        IB_FTYPE_BYTESTR,
        IB_FTYPE_LIST
    } ib_ftype_t;
    typedef enum {
        IB_TXDATA_HTTP_LINE,
        IB_TXDATA_HTTP_HEADER,
        IB_TXDATA_HTTP_BODY,
        IB_TXDATA_HTTP_TRAILER
    } ib_txdata_type_t;
    typedef enum {
        conn_started_event,
        conn_finished_event,
        tx_started_event,
        tx_process_event,
        tx_finished_event,
        handle_context_conn_event,
        handle_connect_event,
        handle_context_tx_event,
        handle_request_headers_event,
        handle_request_event,
        handle_response_headers_event,
        handle_response_event,
        handle_disconnect_event,
        handle_postprocess_event,
        conn_opened_event,
        conn_data_in_event,
        conn_data_out_event,
        conn_closed_event,
        tx_data_in_event,
        tx_data_out_event,
        request_started_event,
        request_headers_event,
        request_body_event,
        request_finished_event,
        response_started_event,
        response_headers_event,
        response_body_event,
        response_finished_event
    } ib_state_event_type_t;


    /* Engine Types */
    typedef struct ib_engine_t ib_engine_t;
    typedef struct ib_context_t ib_context_t;
    typedef struct ib_conn_t ib_conn_t;
    typedef struct ib_conndata_t ib_conndata_t;
    typedef struct ib_tx_t ib_tx_t;
    typedef struct ib_txdata_t ib_txdata_t;
    typedef struct ib_tfn_t ib_tfn_t;
    typedef struct ib_logevent_t ib_logevent_t;
    typedef struct ib_plugin_t ib_plugin_t;
    typedef struct ib_provider_def_t ib_provider_def_t;
    typedef struct ib_provider_t ib_provider_t;
    typedef struct ib_provider_inst_t ib_provider_inst_t;


    /** Function called when a provider is registered. */
    typedef ib_status_t (*ib_provider_register_fn_t)(ib_engine_t *ib,
                                                     ib_provider_t *pr);

    /** Function called when a provider instance is created. */
    typedef ib_status_t (*ib_provider_inst_init_fn_t)(ib_provider_inst_t *pi,
                                                      void *data);

    struct ib_plugin_t {
        int                      vernum;
        int                      abinum;
        const char              *version;
        const char              *filename;
        const char              *name;
    };

    /** Connection Structure */
    struct ib_conn_t {
        ib_engine_t        *ib;
        ib_mpool_t         *mp;
        ib_context_t       *ctx;
        void               *pctx;
        ib_provider_inst_t *dpi;
        ib_hash_t          *data;

        const char         *remote_ipstr;
        int                 remote_port;

        const char         *local_ipstr;
        int                 local_port;

        ib_tx_t            *tx;
        ib_tx_t            *tx_last;

        ib_flags_t          flags;
    };

    /* Connection Data Structure */
    struct ib_conndata_t {
        ib_engine_t        *ib;
        ib_mpool_t         *mp;
        ib_conn_t          *conn;
        size_t              dalloc;
        size_t              dlen;
        const char         *data;
    };

    /* Transaction Structure */
    struct ib_tx_t {
        ib_engine_t        *ib;
        ib_mpool_t         *mp;
        ib_conn_t          *conn;
        ib_context_t       *ctx;
        void               *pctx;
        ib_provider_inst_t *dpi;
        ib_hash_t          *data;
        ib_tx_t            *next;
        const char         *hostname;
        const char         *path;
        ib_flags_t          flags;
    };

    /* Transaction Data Structure */
    struct ib_txdata_t {
        ib_engine_t        *ib;
        ib_mpool_t         *mp;
        ib_tx_t            *tx;
        ib_txdata_type_t    dtype;
        size_t              dalloc;
        size_t              dlen;
        const char         *data;
    };


    /* Data Field Structure */
    struct ib_field_t {
        ib_mpool_t         *mp;
        ib_ftype_t          type;
        const char         *name;
        size_t              nlen;
        void               *pval;
        ib_field_val_t     *val;
    };

    /* Provider Structures */
    struct ib_provider_def_t {
        ib_mpool_t                *mp;
        const char                *type;
        ib_provider_register_fn_t  fn_reg;
        void                      *api;
    };
    struct ib_provider_t {
        ib_engine_t               *ib;
        ib_mpool_t                *mp;
        const char                *type;
        void                      *data;
        void                      *iface;
        void                      *api;
        ib_provider_inst_init_fn_t fn_init;
    };
    struct ib_provider_inst_t {
        ib_mpool_t                *mp;
        ib_provider_t             *pr;
        void                      *data;
    };



    /* Context */
    ib_context_t *ib_context_engine(ib_engine_t *ib);
    ib_context_t *ib_context_main(ib_engine_t *ib);

    /* Byte String */
    size_t ib_bytestr_length(ib_bytestr_t *bs);
    size_t ib_bytestr_size(ib_bytestr_t *bs);
    uint8_t *ib_bytestr_ptr(ib_bytestr_t *bs);

    /* Data Access */
    ib_status_t ib_data_get_ex(ib_provider_inst_t *dpi,
                               const char *name,
                               size_t nlen,
                               ib_field_t **pf);

    /* Misc */
    ib_status_t ib_engine_create(ib_engine_t **pib, void *plugin);
    ib_status_t ib_context_create_main(ib_context_t **pctx,
                                       ib_engine_t *ib);

    /* Logging */
    void ib_clog_ex(ib_context_t *ctx,
                    int level,
                    const char *prefix,
                    const char *file,
                    int line,
                    const char *fmt,
                    ...);
]]

-- Cache lookup of ffi.C
local c = ffi.C

-- 
-- =========================================================================
-- =========================================================================
-- Implementation Notes:
--   * A "l_" prefix is used here to denote a Lua type.
--
--   * A "c_" prefix is used here to denote a C type.
--
--   * A C type still uses zero based indexes.
--
--   * To create a container for outvars, create a single value array:
--
--       c_pfoo = ffi.new("ib_foo_t *[1]"
--
--     Which, in C, is:
--
--       ib_foo_t **pfoo;
--
--   * Dereference via the first index (which, again, is 0, not 1 in C):
--
--       c_foo = c_pfoo[0]
--
--     Which, in C, is:
--
--       foo = *pfoo;
--
--   * Use "local" for function vars or they will be added to the module
--     table, polluting the namespace.
--
-- =========================================================================
-- =========================================================================
-- 

-- ===============================================
-- TODO: Figure out a way around this.
-- ===============================================
function register_module(m)
    base["ironbee-module"] = m
end

-- Lua OO Wrappers around IronBee raw C types
local function newProvider(val)
    local c_val = ffi.cast("ib_provider_t *", val)
    return {
        cvalue = function() return c_val end,
    }
end

local function newProviderInst(val)
    local c_val = ffi.cast("ib_provider_inst_t *", val)
    return {
        cvalue = function() return c_val end,
    }
end

local function newContext(val)
    local c_val = ffi.cast("ib_context_t *", val)
    return {
        cvalue = function() return c_val end,
    }
end

local function newField(val)
    local c_val = ffi.cast("ib_field_t *", val)
    local c_list
    local l_val
    local l_size
    if c_val.type ~= c.IB_FTYPE_LIST then
        l_size = 1
    end
    local t = {
        cvalue = function() return c_val end,
        value = function()
            if l_val ~= nil then
                return l_val
            elseif c_val.type == c.IB_FTYPE_BYTESTR then
                c_fval = ffi.cast("ib_bytestr_t **", c_val.pval)[0]
                l_val = ffi.string(c.ib_bytestr_ptr(c_fval), c.ib_bytestr_length(c_fval))
            elseif c_val.type == c.IB_FTYPE_LIST then
                -- TODO: Loop through and create a table of fields
            elseif c_val.type == c.IB_FTYPE_NULSTR then
                c_fval = ffi.cast("const char **", c_val.pval)[0]
                l_val = ffi.string(c_fval[0])
            elseif c_val.type == c.IB_FTYPE_NUM then
                c_fval = ffi.cast("int64_t **", c_val.pval)[0]
                l_val = base.tonumber(c_fval[0])
            end

            return l_val
        end,
        size = function()
            if l_size ~= nil and c_val.type == c.IB_FTYPE_LIST then
                if c_list == nil then
                    c_list = ffi.cast("ib_list_t **", c_val.pval)[0]
                end
                l_size = base.tonumber(c.ib_list_elements(c_list))
            end
            return l_size
        end,

    }
--     setmetatable(t, {
--         __newindex = function (t, k, v)
--             error("attempt to modify a read-only field", 2)
--         end,
-- --
-- -- TODO This may end up not working with non-numeric indexes as
-- --      there could be a name clash (ie myfield["size"] would
-- --      just execute the size() here and not find the "size"
-- --      field???  And numeric indexes are not that useful. So
-- --      better may be a subfield() method???
-- --
--         __index = function (t, k)
--             -- Be very careful with indexes as there is no protection
--             local ktype = base.type(k)
--             if ktype ~= "number" or ktype <= 0 then
--                 -- TODO Instead loop through returning table of matching fields
--                 error("invalid index \"" .. k .. "\"", 2)
--             end
--             if c_val.type == c.IB_FTYPE_LIST then
--                 local c_idx;
--                 local size = t.size()
--                 local c_node
--                 -- c_list is now available after calling size()
--                 if k > t.size() then
--                     error("index is too large: " .. k, 2)
--                 end
--                 c_idx = ffi.cast("size_t", k)
--                 -- c_node = c.ib_list_node(c_list, c_idx)
--                 -- return newField(ffi.cast("ib_field_t *", c.ib_list_node_data(c_node)))
--             elseif k ~= 1 then
--                 -- Any type has one value
--                 return t.value()
--             end
--         end,
--     })
    return t
end

local function newEngine(val)
    local c_val = ffi.cast("ib_engine_t *", val)
    return {
        cvalue = function() return c_val end,
    }
end

local function newConnData(val)
    local c_val = ffi.cast("ib_conndata_t *", val)
    return {
        cvalue = function() return c_val end,
        dlen = function() return ffi.cast("size_t", c_val.dlen) end,
        data = function() return c_val.data end,
    }
end

local function newConn(val)
    local c_val = ffi.cast("ib_conn_t *", val)
    return {
        cvalue = function() return c_val end,
        dpi = function() return newProviderInst(c_val.dpi) end,
    }
end

local function newTxData(val)
    local c_val = ffi.cast("ib_txdata_t *", val)
    return {
        cvalue = function() return c_val end,
        dtype = function() return ffi.cast("int", c_val.dtype) end,
        dlen = function() return ffi.cast("size_t", c_val.dlen) end,
        data = function() return c_val.data end,
    }
end

local function newTx(val)
    local c_val = ffi.cast("ib_tx_t *", val)
    return {
        cvalue = function() return c_val end,
        dpi = function() return newProviderInst(c_val.dpi) end,
    }
end

-- ===============================================
-- Cast a value as a C "ib_conn_t *".
-- ===============================================
function cast_conn(val)
    return ffi.cast("ib_conn_t *", val);
end

-- ===============================================
-- Cast a value as a C "ib_conndata_t *".
-- ===============================================
function cast_conndata(val)
    return ffi.cast("ib_conndata_t *", val);
end

-- ===============================================
-- Cast a value as a C "ib_tx_t *".
-- ===============================================
function cast_tx(val)
    return ffi.cast("ib_tx_t *", val);
end

-- ===============================================
-- Cast a value as a C "ib_txdata_t *".
-- ===============================================
function cast_txdata(val)
    return ffi.cast("ib_txdata_t *", val);
end

-- ===============================================
-- Cast a value as a C "int".
-- ===============================================
function cast_int(val)
    return ffi.cast("int", val);
end

-- ===============================================
-- Debug Functions.
-- ===============================================
function ib_log_debug(ib, lvl, fmt, ...)
    local c_ctx = c.ib_context_main(ib.cvalue())

    c.ib_clog_ex(c_ctx, 4, "LuaFFI: ", nil, 0, fmt, ...)
end

-- ===============================================
-- Get a data field by name.
-- ===============================================
function ib_data_get(dpi, name)
    local c_dpi = dpi.cvalue()
--    local c_ib = c_dpi.pr.ib
    local c_pf = ffi.new("ib_field_t*[1]")
    local rc
    local c_val

    -- Get the named data field.
    rc = c.ib_data_get_ex(c_dpi, name, string.len(name), c_pf)
    if rc ~= 0 then
        return nil
    end

    return newField(c_pf[0]);
end

-- ===============================================
-- Wrapper function to call Lua event handler.
-- ===============================================
function _IRONBEE_CALL_EVENT_HANDLER(ib, modname, funcname, event, arg, ...)
    local c_ib = ffi.cast("ib_engine_t *", ib);
    local l_ib = newEngine(ib)
    local l_arg
    local m

    if     event == c.conn_started_event then
        l_arg = newConn(arg)
    elseif event == c.conn_finished_event then
        l_arg = newConn(arg)
    elseif event == c.tx_started_event then
        l_arg = newTx(arg)
    elseif event == c.tx_process_event then
        l_arg = newTx(arg)
    elseif event == c.tx_finished_event then
        l_arg = newTx(arg)
    elseif event == c.handle_context_conn_event then
        l_arg = newConn(arg)
    elseif event == c.handle_connect_event then
        l_arg = newConn(arg)
    elseif event == c.handle_context_tx_event then
        l_arg = newTx(arg)
    elseif event == c.handle_request_headers_event then
        l_arg = newTx(arg)
    elseif event == c.handle_request_event then
        l_arg = newTx(arg)
    elseif event == c.handle_response_headers_event then
        l_arg = newTx(arg)
    elseif event == c.handle_response_event then
        l_arg = newTx(arg)
    elseif event == c.handle_disconnect_event then
        l_arg = newConn(arg)
    elseif event == c.handle_postprocess_event then
        l_arg = newConn(arg)
    elseif event == c.conn_opened_event then
        l_arg = newConn(arg)
    elseif event == c.conn_data_in_event then
        l_arg = newConnData(arg)
    elseif event == c.conn_data_out_event then
        l_arg = newConnData(arg)
    elseif event == c.conn_closed_event then
        l_arg = newConn(arg)
    elseif event == c.tx_data_in_event then
        l_arg = newTxData(arg)
    elseif event == c.tx_data_out_event then
        l_arg = newTxData(arg)
    elseif event == c.request_started_event then
        l_arg = newTx(arg)
    elseif event == c.request_headers_event then
        l_arg = newTx(arg)
    elseif event == c.request_body_event then
        l_arg = newTx(arg)
    elseif event == c.request_finished_event then
        l_arg = newTx(arg)
    elseif event == c.response_started_event then
        l_arg = newTx(arg)
    elseif event == c.response_headers_event then
        l_arg = newTx(arg)
    elseif event == c.response_body_event then
        l_arg = newTx(arg)
    elseif event == c.response_finished_event then
        l_arg = newTx(arg)
    else
        ib_log_debug(l_ib, 4, "Unhandled event for module \"%s\": %d",
                     modname, ffi.cast("int", event))
        return nil
    end

--    ib_log_debug(l_ib, 9, "Executing event handler for module \"%s\" event=%d",
--                 modname, ffi.cast("int", event))
    m = modules[modname]
    if m == nil then
        return c.IB_ENOENT
    end

    return m[funcname](l_ib, l_arg, ...)
    -- return c.IB_OK
end
