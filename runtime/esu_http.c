#include "esu_http.h"
#include "esu_json.h"
#include "esu_map.h"
#include "esu_list.h"
#include "esu_mem.h"
#include "esu_error.h"
#include "mongoose.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>     /* tolower */
#include <strings.h>   /* strcasecmp */

/* Case-insensitive substring search (portable; avoids GNU strcasestr). */
static const char *ci_strstr(const char *hay, const char *needle) {
    if (!*needle) return hay;
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            h++; n++;
        }
        if (!*n) return hay;
    }
    return NULL;
}

/* ===== HTTP CLIENT ===== */

/* Shared state for client requests */
typedef struct {
    char *response_body;
    int response_len;
    int done;
    int status;
    EsuValue *headers;   /* parsed response headers (a map), or NULL */
} ClientCtx;

/* Up to this many response headers are captured. */
#define CLIENT_MAX_HEADERS 30

static void client_fn(struct mg_connection *c, int ev, void *ev_data) {
    ClientCtx *ctx = (ClientCtx *)c->fn_data;
    if (ev == MG_EV_CONNECT) {
        /* Connection established — for HTTPS, SSL handshake happens here */
    } else if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        ctx->response_body = esu_alloc(hm->body.len + 1);
        memcpy(ctx->response_body, hm->body.buf, hm->body.len);
        ctx->response_body[hm->body.len] = '\0';
        ctx->response_len = hm->body.len;
        ctx->status = mg_http_status(hm);
        /* Capture response headers into a fresh map. */
        ctx->headers = esu_map_new();
        for (size_t i = 0; i < sizeof(hm->headers) / sizeof(hm->headers[0]); i++) {
            if (hm->headers[i].name.len == 0) break;
            char nbuf[256], vbuf[2048];
            snprintf(nbuf, sizeof(nbuf), "%.*s",
                     (int)hm->headers[i].name.len, hm->headers[i].name.buf);
            snprintf(vbuf, sizeof(vbuf), "%.*s",
                     (int)hm->headers[i].value.len, hm->headers[i].value.buf);
            EsuValue *k = esu_string(nbuf), *v = esu_string(vbuf);
            esu_map_set(ctx->headers, k, v);
            esu_release(k);
            esu_release(v);
        }
        ctx->done = 1;
        c->is_closing = 1;
    } else if (ev == MG_EV_ERROR) {
        ctx->done = 1;
        c->is_closing = 1;
    }
}

/* Perform an HTTP request.
 *   method        - "GET", "POST", "PUT", "DELETE", "PATCH", ...
 *   url            - full URL
 *   body           - request body string, or NULL
 *   extra_headers  - "Header: val\r\n..." block to send verbatim, or NULL
 *   content_type   - sets Content-Type if non-NULL AND not already in extra_headers
 * Returns a response map {status, body, headers, ok} (ok = status < 400), or
 * nothing on connection failure. */
static EsuValue *http_request(const char *url, const char *method,
                              const char *body, const char *extra_headers,
                              const char *content_type) {
    struct mg_mgr mgr;
    ClientCtx ctx = {NULL, 0, 0, 0, NULL};
    mg_mgr_init(&mgr);

    struct mg_str host = mg_url_host(url);

    /* Assemble outgoing header block. */
    char headers[2048] = "";
    size_t hlen = 0;
    int has_ct = extra_headers &&
                 (ci_strstr(extra_headers, "content-type:") != NULL);
    if (content_type && !has_ct) {
        int n = snprintf(headers, sizeof(headers),
                         "Content-Type: %s\r\n", content_type);
        if (n > 0) hlen = (size_t)n;
    }
    if (extra_headers && extra_headers[0]) {
        snprintf(headers + hlen, sizeof(headers) - hlen, "%s", extra_headers);
    }

    struct mg_connection *c = mg_http_connect(&mgr, url, client_fn, &ctx);
    if (!c) {
        mg_mgr_free(&mgr);
        esu_raise_msg("cannot connect to URL");
        return esu_nothing();
    }

    int is_ssl = (strncmp(url, "https", 5) == 0);
    if (is_ssl) {
        struct mg_tls_opts opts = {0};
        mg_tls_init(c, &opts);
    }

    if (body) {
        mg_printf(c,
            "%s %s HTTP/1.1\r\n"
            "Host: %.*s\r\n"
            "%s"
            "Content-Length: %d\r\n"
            "\r\n"
            "%s",
            method, mg_url_uri(url),
            (int)host.len, host.buf,
            headers,
            (int)strlen(body), body);
    } else {
        mg_printf(c,
            "%s %s HTTP/1.1\r\n"
            "Host: %.*s\r\n"
            "%s"
            "\r\n",
            method, mg_url_uri(url),
            (int)host.len, host.buf,
            headers);
    }

    while (!ctx.done) mg_mgr_poll(&mgr, 1000);
    mg_mgr_free(&mgr);

    if (!ctx.response_body) {
        if (ctx.headers) esu_release(ctx.headers);
        return esu_nothing();
    }

    EsuValue *result = esu_map_new();
    EsuValue *body_val = esu_string(ctx.response_body);
    EsuValue *status_val = esu_number(ctx.status);
    EsuValue *ok_val = esu_bool(ctx.status > 0 && ctx.status < 400);
    esu_map_set(result, esu_string("body"), body_val);
    esu_map_set(result, esu_string("status"), status_val);
    esu_map_set(result, esu_string("ok"), ok_val);
    esu_release(body_val);
    esu_release(status_val);
    esu_release(ok_val);

    if (ctx.headers) {
        esu_map_set(result, esu_string("headers"), ctx.headers);
        /* If the server says JSON, try to expose a parsed `json` field — but
         * swallow parse failures silently. We pre-validate with cJSON so the
         * later esu_json_parse call never raises. */
        for (int i = 0; i < ctx.headers->as.map->count; i++) {
            if (strcasecmp(ctx.headers->as.map->entries[i].key, "Content-Type") == 0) {
                char *ctv = esu_to_cstring(ctx.headers->as.map->entries[i].value);
                if (ci_strstr(ctv, "application/json")) {
                    cJSON *probe = cJSON_Parse(ctx.response_body);
                    if (probe) {
                        cJSON_Delete(probe);
                        EsuValue *bstr = esu_string(ctx.response_body);
                        EsuValue *parsed = esu_json_parse(bstr);
                        esu_map_set(result, esu_string("json"), parsed);
                        esu_release(bstr);
                        esu_release(parsed);
                    }
                    /* else: response said JSON but body isn't valid; skip silently. */
                }
                esu_free(ctv);
                break;
            }
        }
        esu_release(ctx.headers);
    } else {
        EsuValue *empty = esu_map_new();
        esu_map_set(result, esu_string("headers"), empty);
        esu_release(empty);
    }

    esu_free(ctx.response_body);
    return result;
}

/* Build a "Header: value\r\n..." block from an EsuValue map. Caller frees.
 * Returns NULL if `m` isn't a map or is empty. */
static char *headers_block_from_map(EsuValue *m) {
    if (!m || m->type != VAL_MAP || m->as.map->count == 0) return NULL;
    size_t cap = 256, len = 0;
    char *buf = esu_alloc(cap);
    buf[0] = '\0';
    EsuMap *map = m->as.map;
    for (int i = 0; i < map->count; i++) {
        char *vstr = esu_to_cstring(map->entries[i].value);
        size_t need = strlen(map->entries[i].key) + strlen(vstr) + 8;
        while (len + need >= cap) { cap *= 2; buf = esu_realloc(buf, cap); }
        len += (size_t)snprintf(buf + len, cap - len, "%s: %s\r\n",
                                map->entries[i].key, vstr);
        esu_free(vstr);
    }
    return buf;
}

EsuValue *esu_http_get(EsuValue *url) {
    if (url->type != VAL_STRING) {
        esu_raise_msg("URL must be a string");
        return esu_nothing();
    }
    return http_request(url->as.string, "GET", NULL, NULL, NULL);
}

EsuValue *esu_http_post(EsuValue *url, EsuValue *body) {
    if (url->type != VAL_STRING) {
        esu_raise_msg("URL must be a string");
        return esu_nothing();
    }
    char *body_str = esu_to_cstring(body);
    EsuValue *result = http_request(url->as.string, "POST", body_str,
                                    NULL, "application/json");
    esu_free(body_str);
    return result;
}

EsuValue *esu_http_put(EsuValue *url, EsuValue *body) {
    if (url->type != VAL_STRING) { esu_raise_msg("URL must be a string"); return esu_nothing(); }
    char *body_str = esu_to_cstring(body);
    EsuValue *result = http_request(url->as.string, "PUT", body_str,
                                    NULL, "application/json");
    esu_free(body_str);
    return result;
}

EsuValue *esu_http_patch(EsuValue *url, EsuValue *body) {
    if (url->type != VAL_STRING) { esu_raise_msg("URL must be a string"); return esu_nothing(); }
    char *body_str = esu_to_cstring(body);
    EsuValue *result = http_request(url->as.string, "PATCH", body_str,
                                    NULL, "application/json");
    esu_free(body_str);
    return result;
}

EsuValue *esu_http_delete(EsuValue *url) {
    if (url->type != VAL_STRING) { esu_raise_msg("URL must be a string"); return esu_nothing(); }
    return http_request(url->as.string, "DELETE", NULL, NULL, NULL);
}

/* General request: __http_request(method, url, options).
 * options (a map, optional): body, headers (map), json (bool — JSON-encode body). */
EsuValue *esu_http_request(EsuValue *method, EsuValue *url, EsuValue *options) {
    if (url->type != VAL_STRING) {
        esu_raise_msg("URL must be a string");
        return esu_nothing();
    }
    const char *m = (method->type == VAL_STRING) ? method->as.string : "GET";

    char *body_str = NULL;
    char *extra_headers = NULL;
    const char *content_type = NULL;

    if (options && options->type == VAL_MAP) {
        EsuValue *body = esu_map_get(options, esu_string("body"));
        EsuValue *hdrs = esu_map_get(options, esu_string("headers"));
        EsuValue *as_json = esu_map_get(options, esu_string("json"));
        int want_json = as_json && ((as_json->type == VAL_BOOL && as_json->as.boolean) ||
                                    (as_json->type == VAL_NUMBER && as_json->as.number != 0));
        if (body && body->type != VAL_NOTHING) {
            if (want_json) {
                EsuValue *encoded = esu_to_json(body);
                body_str = esu_to_cstring(encoded);
                esu_release(encoded);
                content_type = "application/json";
            } else {
                body_str = esu_to_cstring(body);
            }
        }
        extra_headers = headers_block_from_map(hdrs);
        if (body) esu_release(body);
        if (hdrs) esu_release(hdrs);
        if (as_json) esu_release(as_json);
    }

    EsuValue *result = http_request(url->as.string, m, body_str,
                                    extra_headers, content_type);
    esu_free(body_str);
    esu_free(extra_headers);
    return result;
}

/* ===== WEBSOCKET CLIENT ===== */

/* A blocking WS client: opens, then provides send/receive/close on top of a
 * private mongoose event manager that's polled inline. Single-threaded — the
 * caller's Esu program is paused while we wait for frames.
 *
 * The handle is a map { __mgr, __conn, open, queue } stored as a hidden
 * pointer so we don't expose mongoose internals to Esu code. */
typedef struct {
    struct mg_mgr mgr;
    struct mg_connection *conn;
    int open;              /* 1 while the underlying connection is alive */

    /* Inbound text-frame queue. Frames are heap strings; freed on dequeue. */
    char **queue;
    int    queue_count;
    int    queue_capacity;
} WsClient;

static void ws_client_enqueue(WsClient *wc, const char *buf, size_t len) {
    if (wc->queue_count >= wc->queue_capacity) {
        wc->queue_capacity = wc->queue_capacity == 0 ? 4 : wc->queue_capacity * 2;
        wc->queue = esu_realloc(wc->queue, sizeof(char *) * wc->queue_capacity);
    }
    char *s = esu_alloc(len + 1);
    memcpy(s, buf, len);
    s[len] = '\0';
    wc->queue[wc->queue_count++] = s;
}

static char *ws_client_dequeue(WsClient *wc) {
    if (wc->queue_count == 0) return NULL;
    char *s = wc->queue[0];
    for (int i = 0; i < wc->queue_count - 1; i++) wc->queue[i] = wc->queue[i + 1];
    wc->queue_count--;
    return s;
}

static void ws_client_fn(struct mg_connection *c, int ev, void *ev_data) {
    WsClient *wc = (WsClient *)c->fn_data;
    if (ev == MG_EV_WS_OPEN) {
        wc->open = 1;
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
        ws_client_enqueue(wc, wm->data.buf, wm->data.len);
    } else if (ev == MG_EV_CLOSE || ev == MG_EV_ERROR) {
        wc->open = 0;
    }
}

/* Open a WS connection. Blocks until handshake completes (or fails). */
EsuValue *esu_ws_open(EsuValue *url) {
    if (!url || url->type != VAL_STRING) {
        esu_raise_msg("websocket URL must be a string");
        return esu_nothing();
    }
    WsClient *wc = esu_alloc(sizeof(WsClient));
    wc->conn = NULL;
    wc->open = 0;
    wc->queue = NULL;
    wc->queue_count = 0;
    wc->queue_capacity = 0;
    mg_mgr_init(&wc->mgr);

    wc->conn = mg_ws_connect(&wc->mgr, url->as.string, ws_client_fn, wc, NULL);
    if (!wc->conn) {
        mg_mgr_free(&wc->mgr);
        esu_free(wc);
        esu_raise_msg("cannot connect to websocket URL");
        return esu_nothing();
    }

    /* Poll until the handshake completes or the connection is rejected. */
    for (int i = 0; i < 50 && !wc->open && wc->conn; i++) {
        mg_mgr_poll(&wc->mgr, 100);
        /* If the connection died during handshake, mongoose will null it on us
         * via is_closing → next poll cleans it up; we detect via wc->open=0
         * after the loop. */
    }

    EsuValue *handle = esu_map_new();
    esu_map_set(handle, esu_string("url"), url);
    esu_map_set(handle, esu_string("__client"), esu_number((double)(uintptr_t)wc));
    return handle;
}

static WsClient *ws_client_from(EsuValue *handle) {
    if (!handle || handle->type != VAL_MAP) return NULL;
    EsuValue *p = esu_map_get(handle, esu_string("__client"));
    if (!p || p->type != VAL_NUMBER) {
        if (p) esu_release(p);
        return NULL;
    }
    WsClient *wc = (WsClient *)(uintptr_t)p->as.number;
    esu_release(p);
    return wc;
}

void esu_ws_client_send(EsuValue *handle, EsuValue *message) {
    WsClient *wc = ws_client_from(handle);
    if (!wc || !wc->conn) { esu_raise_msg("websocket is not open"); return; }
    char *msg = esu_to_cstring(message);
    mg_ws_send(wc->conn, msg, strlen(msg), WEBSOCKET_OP_TEXT);
    esu_free(msg);
    /* Flush the send by polling once. */
    mg_mgr_poll(&wc->mgr, 10);
}

/* Block until a frame arrives or the connection closes. Returns the message
 * (as a string) or nothing on close. */
EsuValue *esu_ws_client_receive(EsuValue *handle) {
    WsClient *wc = ws_client_from(handle);
    if (!wc) { esu_raise_msg("invalid websocket handle"); return esu_nothing(); }
    while (wc->queue_count == 0 && wc->open) {
        mg_mgr_poll(&wc->mgr, 100);
    }
    char *s = ws_client_dequeue(wc);
    if (!s) return esu_nothing();
    EsuValue *v = esu_string(s);
    esu_free(s);
    return v;
}

/* __ws_receive → esu_ws_receive — convenience alias so the builtin namespace
 * is uniform (everything else WS-related is __ws_*). */
EsuValue *esu_ws_receive(EsuValue *handle) {
    return esu_ws_client_receive(handle);
}

void esu_ws_client_close(EsuValue *handle) {
    WsClient *wc = ws_client_from(handle);
    if (!wc) return;
    if (wc->conn) {
        wc->conn->is_closing = 1;
        mg_mgr_poll(&wc->mgr, 50);
    }
    /* Drain queue */
    for (int i = 0; i < wc->queue_count; i++) esu_free(wc->queue[i]);
    esu_free(wc->queue);
    mg_mgr_free(&wc->mgr);
    esu_free(wc);
    /* Zero the hidden pointer so a second close is a no-op. */
    EsuValue *zero = esu_number(0);
    esu_map_set(handle, esu_string("__client"), zero);
    esu_release(zero);
}

/* ===== HTTP SERVER ===== */

typedef struct {
    char *method;   /* "GET", "POST", etc. */
    char *path;     /* "/hello", "/api/users", etc. */
    EsuValue *handler; /* Closure to call */
} Route;

/* A WebSocket route.
 *
 * `connect_handler` runs once per new connection — typical use is to register
 * per-connection on-message/on-close handlers via __ws_on_message etc.
 * `message_handler`, if set, is used directly as the per-message handler for
 * every connection on this route (a simpler form than connect_handler for
 * stateless handlers). At least one of the two must be non-NULL.
 */
typedef struct {
    char *path;
    EsuValue *connect_handler;
    EsuValue *message_handler;  /* optional — assigned to each new conn */
} WsRoute;

/* Per-connection state for a WebSocket connection. */
typedef struct {
    struct mg_connection *c;     /* mongoose connection */
    EsuValue *message_handler;   /* closure(connection, message), or NULL */
    EsuValue *close_handler;     /* closure(connection), or NULL */
} WsConn;

typedef struct {
    Route *routes;
    int route_count;
    int route_capacity;
    int port;
    char *static_dir;       /* directory to serve static files from, or NULL */
    char *static_prefix;    /* URL prefix the static dir is mounted at, or NULL ("/") */
    EsuValue *before_hook;  /* closure run before every request, or NULL */

    /* WebSocket routes (HTTP-handshake paths that upgrade to WS). */
    WsRoute *ws_routes;
    int ws_route_count;
    int ws_route_capacity;

    /* Live WebSocket connections — kept so broadcast() can reach them all. */
    WsConn **ws_conns;
    int ws_conn_count;
    int ws_conn_capacity;
} ServerCtx;

static ServerCtx *active_server = NULL;
/* Connection currently dispatching a WS message / close — read by send/close
 * builtins inside handlers. */
static WsConn *active_ws_conn = NULL;

/* Hex digit → int. Returns -1 on invalid. */
static int hex_val(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

/* URL-decode src[0..len) into a freshly allocated NUL-terminated string. */
static char *url_decode(const char *src, int len) {
    char *out = esu_alloc(len + 1);
    int j = 0;
    for (int i = 0; i < len; i++) {
        char c = src[i];
        if (c == '+') {
            out[j++] = ' ';
        } else if (c == '%' && i + 2 < len) {
            int hi = hex_val((unsigned char)src[i + 1]);
            int lo = hex_val((unsigned char)src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out[j++] = (char)((hi << 4) | lo);
                i += 2;
            } else {
                out[j++] = c;
            }
        } else {
            out[j++] = c;
        }
    }
    out[j] = '\0';
    return out;
}

/* Parse "k1=v1&k2=v2" into a map. Empty/missing values become "". */
static EsuValue *parse_query_string(const char *src, int len) {
    EsuValue *map = esu_map_new();
    int i = 0;
    while (i < len) {
        int key_start = i;
        while (i < len && src[i] != '=' && src[i] != '&') i++;
        int key_end = i;
        int val_start = i, val_end = i;
        if (i < len && src[i] == '=') {
            i++;
            val_start = i;
            while (i < len && src[i] != '&') i++;
            val_end = i;
        }
        if (key_end > key_start) {
            char *k = url_decode(src + key_start, key_end - key_start);
            char *v = url_decode(src + val_start, val_end - val_start);
            EsuValue *kv = esu_string(k);
            EsuValue *vv = esu_string(v);
            esu_map_set(map, kv, vv);
            esu_release(kv);
            esu_release(vv);
            esu_free(k);
            esu_free(v);
        }
        if (i < len && src[i] == '&') i++;
    }
    return map;
}

/* Parse a "Cookie: a=1; b=2" header value into a map. */
static EsuValue *parse_cookies(struct mg_http_message *hm) {
    EsuValue *map = esu_map_new();
    struct mg_str *cookie = mg_http_get_header(hm, "Cookie");
    if (!cookie) return map;
    const char *src = cookie->buf;
    int len = (int)cookie->len;
    int i = 0;
    while (i < len) {
        /* skip leading spaces / semicolons */
        while (i < len && (src[i] == ' ' || src[i] == ';')) i++;
        int key_start = i;
        while (i < len && src[i] != '=' && src[i] != ';') i++;
        int key_end = i;
        int val_start = i, val_end = i;
        if (i < len && src[i] == '=') {
            i++;
            val_start = i;
            while (i < len && src[i] != ';') i++;
            val_end = i;
        }
        if (key_end > key_start) {
            char *k = url_decode(src + key_start, key_end - key_start);
            char *v = url_decode(src + val_start, val_end - val_start);
            EsuValue *kv = esu_string(k);
            EsuValue *vv = esu_string(v);
            esu_map_set(map, kv, vv);
            esu_release(kv);
            esu_release(vv);
            esu_free(k);
            esu_free(v);
        }
    }
    return map;
}

/* Parse mongoose headers into a map (case-preserved, last-wins). */
static EsuValue *parse_headers(struct mg_http_message *hm) {
    EsuValue *map = esu_map_new();
    for (size_t i = 0; i < sizeof(hm->headers) / sizeof(hm->headers[0]); i++) {
        if (hm->headers[i].name.len == 0) break;
        char name_buf[256];
        char val_buf[1024];
        snprintf(name_buf, sizeof(name_buf), "%.*s",
                 (int)hm->headers[i].name.len, hm->headers[i].name.buf);
        snprintf(val_buf, sizeof(val_buf), "%.*s",
                 (int)hm->headers[i].value.len, hm->headers[i].value.buf);
        EsuValue *k = esu_string(name_buf);
        EsuValue *v = esu_string(val_buf);
        esu_map_set(map, k, v);
        esu_release(k);
        esu_release(v);
    }
    return map;
}

/* Match request path against a route pattern with :name or {name} placeholders.
 * On match, returns a freshly allocated map of captured params (caller releases).
 * On no match, returns NULL. */
static EsuValue *match_route(const char *pattern, const char *path) {
    EsuValue *params = esu_map_new();
    const char *p = pattern;
    const char *u = path;
    while (*p && *u) {
        if (*p == ':' || *p == '{') {
            char close = (*p == '{') ? '}' : '/';
            p++;
            const char *name_start = p;
            while (*p && *p != close && !(*p == '/' && close != '/')) p++;
            int name_len = (int)(p - name_start);
            if (close == '}' && *p == '}') p++;
            const char *val_start = u;
            while (*u && *u != '/') u++;
            int val_len = (int)(u - val_start);
            char name_buf[128];
            int n = name_len < (int)sizeof(name_buf) - 1 ? name_len : (int)sizeof(name_buf) - 1;
            memcpy(name_buf, name_start, n);
            name_buf[n] = '\0';
            char *val = esu_alloc(val_len + 1);
            memcpy(val, val_start, val_len);
            val[val_len] = '\0';
            EsuValue *k = esu_string(name_buf);
            EsuValue *v = esu_string(val);
            esu_map_set(params, k, v);
            esu_release(k);
            esu_release(v);
            esu_free(val);
        } else if (*p == *u) {
            p++; u++;
        } else {
            esu_release(params);
            return NULL;
        }
    }
    if (*p == '\0' && *u == '\0') return params;
    esu_release(params);
    return NULL;
}

/* Build a request map from mongoose's http message */
static EsuValue *build_request(struct mg_http_message *hm) {
    EsuValue *req = esu_map_new();

    /* Path */
    char path_buf[2048];
    snprintf(path_buf, sizeof(path_buf), "%.*s", (int)hm->uri.len, hm->uri.buf);
    esu_map_set(req, esu_string("path"), esu_string(path_buf));

    /* Method */
    char method_buf[16];
    snprintf(method_buf, sizeof(method_buf), "%.*s",
             (int)hm->method.len, hm->method.buf);
    esu_map_set(req, esu_string("method"), esu_string(method_buf));

    /* Body */
    if (hm->body.len > 0) {
        char *body = esu_alloc(hm->body.len + 1);
        memcpy(body, hm->body.buf, hm->body.len);
        body[hm->body.len] = '\0';
        esu_map_set(req, esu_string("body"), esu_string(body));
        esu_free(body);
    } else {
        esu_map_set(req, esu_string("body"), esu_string(""));
    }

    /* Query string */
    EsuValue *query = parse_query_string(hm->query.buf, (int)hm->query.len);
    esu_map_set(req, esu_string("query"), query);
    esu_release(query);

    /* Headers */
    EsuValue *headers = parse_headers(hm);
    esu_map_set(req, esu_string("headers"), headers);
    esu_release(headers);

    /* Cookies */
    EsuValue *cookies = parse_cookies(hm);
    esu_map_set(req, esu_string("cookies"), cookies);
    esu_release(cookies);

    /* Route params placeholder — populated by server_fn after route match */
    EsuValue *params = esu_map_new();
    esu_map_set(req, esu_string("params"), params);
    esu_release(params);

    return req;
}

/* Append to a growing heap string. Updates *buf, *cap, *len. */
static void str_append(char **buf, size_t *cap, size_t *len, const char *fmt, ...) {
    va_list ap;
    for (;;) {
        va_start(ap, fmt);
        int n = vsnprintf(*buf + *len, *cap - *len, fmt, ap);
        va_end(ap);
        if (n < 0) return;
        if ((size_t)n < *cap - *len) { *len += (size_t)n; return; }
        *cap = (*cap + (size_t)n + 1) * 2;
        *buf = esu_realloc(*buf, *cap);
    }
}

/* Build "Header-Name: value\r\n..." for a headers map plus a cookies map.
 * Sets *has_content_type if the headers map contains Content-Type.
 * Caller frees the returned string. */
static char *build_response_headers(EsuValue *headers_map, EsuValue *cookies_map,
                                    int *has_content_type) {
    *has_content_type = 0;
    size_t cap = 256, len = 0;
    char *buf = esu_alloc(cap);
    buf[0] = '\0';

    if (headers_map && headers_map->type == VAL_MAP) {
        EsuMap *map = headers_map->as.map;
        for (int i = 0; i < map->count; i++) {
            char *vstr = esu_to_cstring(map->entries[i].value);
            if (strcasecmp(map->entries[i].key, "Content-Type") == 0) {
                *has_content_type = 1;
            }
            str_append(&buf, &cap, &len, "%s: %s\r\n", map->entries[i].key, vstr);
            esu_free(vstr);
        }
    }
    if (cookies_map && cookies_map->type == VAL_MAP) {
        EsuMap *map = cookies_map->as.map;
        for (int i = 0; i < map->count; i++) {
            char *vstr = esu_to_cstring(map->entries[i].value);
            /* Sensible defaults: site-wide path, HttpOnly */
            str_append(&buf, &cap, &len, "Set-Cookie: %s=%s; Path=/; HttpOnly\r\n",
                       map->entries[i].key, vstr);
            esu_free(vstr);
        }
    }
    return buf;
}

/* Send an EsuValue response (string, map with status/body/headers/cookies, or
 * nothing) over the connection. Consumes nothing — caller still owns `response`. */
static void send_esu_response(struct mg_connection *c, EsuValue *response) {
    int code = 200;
    char *body_str = NULL;
    char *extra_headers = esu_strdup("");
    int has_content_type = 0;

    if (response && response->type == VAL_MAP) {
        EsuValue *status = esu_map_get(response, esu_string("status"));
        EsuValue *body = esu_map_get(response, esu_string("body"));
        EsuValue *headers = esu_map_get(response, esu_string("headers"));
        EsuValue *cookies = esu_map_get(response, esu_string("cookies"));
        if (status && status->type == VAL_NUMBER) code = (int)status->as.number;
        body_str = (body && body->type != VAL_NOTHING) ? esu_to_cstring(body)
                                                       : esu_strdup("");
        esu_free(extra_headers);
        extra_headers = build_response_headers(headers, cookies, &has_content_type);
        if (status) esu_release(status);
        if (body) esu_release(body);
        if (headers) esu_release(headers);
        if (cookies) esu_release(cookies);
    } else if (response && response->type == VAL_STRING) {
        body_str = esu_strdup(response->as.string);
    } else {
        body_str = esu_strdup("");
    }

    /* Default Content-Type unless the handler set one (and unless body empty,
     * e.g. a bare redirect). */
    size_t hcap = 512, hlen = 0;
    char *hbuf = esu_alloc(hcap);
    hbuf[0] = '\0';
    if (!has_content_type && body_str[0] != '\0') {
        str_append(&hbuf, &hcap, &hlen, "Content-Type: text/plain\r\n");
    }
    str_append(&hbuf, &hcap, &hlen, "%s", extra_headers);
    mg_http_reply(c, code, hbuf, "%s", body_str);

    esu_free(hbuf);
    esu_free(body_str);
    esu_free(extra_headers);
}

/* True if `response` represents an actual response the server should send
 * (a string or a non-empty map), as opposed to "nothing"/empty. */
static int is_real_response(EsuValue *response) {
    if (!response) return 0;
    if (response->type == VAL_STRING) return 1;
    if (response->type == VAL_MAP && response->as.map->count > 0) return 1;
    return 0;
}

/* Find a WS route on `ctx` whose path matches `uri`. NULL if none. */
static WsRoute *find_ws_route(ServerCtx *ctx, const char *uri) {
    for (int i = 0; i < ctx->ws_route_count; i++) {
        if (strcmp(ctx->ws_routes[i].path, uri) == 0) return &ctx->ws_routes[i];
        /* also allow mongoose-style globs */
        if (mg_match(mg_str(uri), mg_str(ctx->ws_routes[i].path), NULL)) {
            return &ctx->ws_routes[i];
        }
    }
    return NULL;
}

/* Register a new live WS connection on a server. Takes ownership of `wc`. */
static void ws_conns_add(ServerCtx *ctx, WsConn *wc) {
    if (ctx->ws_conn_count >= ctx->ws_conn_capacity) {
        ctx->ws_conn_capacity = ctx->ws_conn_capacity == 0 ? 8 : ctx->ws_conn_capacity * 2;
        ctx->ws_conns = esu_realloc(ctx->ws_conns, sizeof(WsConn *) * ctx->ws_conn_capacity);
    }
    ctx->ws_conns[ctx->ws_conn_count++] = wc;
}

/* Find a connection record by its mongoose pointer. */
static int ws_conns_find(ServerCtx *ctx, struct mg_connection *c) {
    for (int i = 0; i < ctx->ws_conn_count; i++) {
        if (ctx->ws_conns[i]->c == c) return i;
    }
    return -1;
}

/* Drop and free a WS connection record by index. */
static void ws_conns_remove_at(ServerCtx *ctx, int idx) {
    if (idx < 0 || idx >= ctx->ws_conn_count) return;
    WsConn *wc = ctx->ws_conns[idx];
    if (wc->message_handler) esu_release(wc->message_handler);
    if (wc->close_handler) esu_release(wc->close_handler);
    esu_free(wc);
    /* compact */
    for (int j = idx; j < ctx->ws_conn_count - 1; j++) {
        ctx->ws_conns[j] = ctx->ws_conns[j + 1];
    }
    ctx->ws_conn_count--;
}

/* Build a "connection" Esu value (a map with a hidden mg_connection*). */
static EsuValue *make_connection_value(struct mg_connection *c) {
    EsuValue *m = esu_map_new();
    esu_map_set(m, esu_string("__conn"), esu_number((double)(uintptr_t)c));
    return m;
}

/* Resolve the hidden mg_connection* from a connection value. */
static struct mg_connection *conn_from_value(EsuValue *v) {
    if (!v || v->type != VAL_MAP) return NULL;
    EsuValue *p = esu_map_get(v, esu_string("__conn"));
    if (!p || p->type != VAL_NUMBER) {
        if (p) esu_release(p);
        return NULL;
    }
    struct mg_connection *c = (struct mg_connection *)(uintptr_t)p->as.number;
    esu_release(p);
    return c;
}

static void server_fn(struct mg_connection *c, int ev, void *ev_data) {
    ServerCtx *ctx = (ServerCtx *)c->fn_data;

    /* === HTTP request === */
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;

        char uri_buf[2048];
        snprintf(uri_buf, sizeof(uri_buf), "%.*s", (int)hm->uri.len, hm->uri.buf);

        /* WS upgrade: if this URI matches a WS route, complete the handshake
         * here, allocate per-connection state, and either run the connect
         * handler (for the connect/on-message form) or pre-install the
         * route's message handler directly. */
        WsRoute *wr = find_ws_route(ctx, uri_buf);
        if (wr && mg_http_get_header(hm, "Upgrade")) {
            mg_ws_upgrade(c, hm, NULL);

            WsConn *wc = esu_alloc(sizeof(WsConn));
            wc->c = c;
            wc->message_handler = wr->message_handler ? esu_retain(wr->message_handler) : NULL;
            wc->close_handler = NULL;
            ws_conns_add(ctx, wc);

            if (wr->connect_handler) {
                EsuValue *conn_val = make_connection_value(c);
                EsuValue *args[1] = { conn_val };
                ServerCtx *saved_server = active_server;
                WsConn *saved_conn = active_ws_conn;
                active_server = ctx;
                active_ws_conn = wc;
                EsuValue *result = esu_call(wr->connect_handler, args, 1);
                active_server = saved_server;
                active_ws_conn = saved_conn;
                if (result) esu_release(result);
                esu_release(conn_val);
            }
            return;
        }

        /* 1. Before-hook (middleware). */
        if (ctx->before_hook) {
            EsuValue *request = build_request(hm);
            EsuValue *args[1] = { request };
            EsuValue *response = esu_call(ctx->before_hook, args, 1);
            if (is_real_response(response)) {
                send_esu_response(c, response);
                if (response) esu_release(response);
                esu_release(request);
                return;
            }
            if (response) esu_release(response);
            esu_release(request);
        }

        /* 2. Match a registered route. */
        for (int i = 0; i < ctx->route_count; i++) {
            Route *r = &ctx->routes[i];
            int method_match = (strcmp(r->method, "*") == 0) ||
                (mg_strcmp(hm->method, mg_str(r->method)) == 0);
            if (!method_match) continue;

            EsuValue *route_params = match_route(r->path, uri_buf);
            int matched = (route_params != NULL);
            if (!matched && mg_match(hm->uri, mg_str(r->path), NULL)) {
                matched = 1;
                route_params = esu_map_new();
            }
            if (!matched) continue;

            EsuValue *request = build_request(hm);
            esu_map_set(request, esu_string("params"), route_params);
            esu_release(route_params);

            EsuValue *args[1] = { request };
            EsuValue *response = esu_call(r->handler, args, 1);
            send_esu_response(c, response);
            if (response) esu_release(response);
            esu_release(request);
            return;
        }

        /* 3. Static file fallback. */
        if (ctx->static_dir) {
            const char *prefix = ctx->static_prefix ? ctx->static_prefix : "/";
            size_t plen = strlen(prefix);
            if (strncmp(uri_buf, prefix, plen) == 0) {
                struct mg_http_serve_opts opts = {0};
                opts.root_dir = ctx->static_dir;
                if (plen > 1) {
                    char stripped[2048];
                    const char *rest = uri_buf + plen;
                    snprintf(stripped, sizeof(stripped), "/%s",
                             rest[0] == '/' ? rest + 1 : rest);
                    struct mg_http_message hm2 = *hm;
                    hm2.uri = mg_str(stripped);
                    mg_http_serve_dir(c, &hm2, &opts);
                } else {
                    mg_http_serve_dir(c, hm, &opts);
                }
                return;
            }
        }

        /* 4. No match. */
        mg_http_reply(c, 404, "", "Not Found");
        return;
    }

    /* === WebSocket message === */
    if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
        int idx = ws_conns_find(ctx, c);
        if (idx < 0) return;
        WsConn *wc = ctx->ws_conns[idx];
        if (!wc->message_handler) return;

        /* Copy frame into a NUL-terminated string. */
        char *buf = esu_alloc(wm->data.len + 1);
        memcpy(buf, wm->data.buf, wm->data.len);
        buf[wm->data.len] = '\0';

        EsuValue *conn_val = make_connection_value(c);
        EsuValue *msg_val = esu_string(buf);
        EsuValue *args[2] = { conn_val, msg_val };
        ServerCtx *saved_server = active_server;
        WsConn *saved_conn = active_ws_conn;
        active_server = ctx;
        active_ws_conn = wc;
        EsuValue *result = esu_call(wc->message_handler, args, 2);
        active_server = saved_server;
        active_ws_conn = saved_conn;
        if (result) esu_release(result);
        esu_release(conn_val);
        esu_release(msg_val);
        esu_free(buf);
        return;
    }

    /* === Connection close (HTTP or WS) === */
    if (ev == MG_EV_CLOSE) {
        int idx = ws_conns_find(ctx, c);
        if (idx >= 0) {
            WsConn *wc = ctx->ws_conns[idx];
            if (wc->close_handler) {
                EsuValue *conn_val = make_connection_value(c);
                EsuValue *args[1] = { conn_val };
                ServerCtx *saved_server = active_server;
                WsConn *saved_conn = active_ws_conn;
                active_server = ctx;
                active_ws_conn = wc;
                EsuValue *result = esu_call(wc->close_handler, args, 1);
                active_server = saved_server;
                active_ws_conn = saved_conn;
                if (result) esu_release(result);
                esu_release(conn_val);
            }
            ws_conns_remove_at(ctx, idx);
        }
        return;
    }
}

EsuValue *esu_http_create_server(EsuValue *port) {
    int p = (port->type == VAL_NUMBER) ? (int)port->as.number : 8080;
    ServerCtx *ctx = esu_alloc(sizeof(ServerCtx));
    ctx->routes = NULL;
    ctx->route_count = 0;
    ctx->route_capacity = 0;
    ctx->port = p;
    ctx->static_dir = NULL;
    ctx->static_prefix = NULL;
    ctx->before_hook = NULL;
    ctx->ws_routes = NULL;
    ctx->ws_route_count = 0;
    ctx->ws_route_capacity = 0;
    ctx->ws_conns = NULL;
    ctx->ws_conn_count = 0;
    ctx->ws_conn_capacity = 0;

    /* Store server context as a map with a hidden pointer */
    EsuValue *server = esu_map_new();
    esu_map_set(server, esu_string("port"), esu_number(p));
    esu_map_set(server, esu_string("__ctx"), esu_number((double)(uintptr_t)ctx));
    return server;
}

/* Resolve the hidden ServerCtx pointer stored on a server map. */
static ServerCtx *server_ctx(EsuValue *server) {
    EsuValue *ctx_val = esu_map_get(server, esu_string("__ctx"));
    if (!ctx_val || ctx_val->type != VAL_NUMBER) {
        if (ctx_val) esu_release(ctx_val);
        return NULL;
    }
    ServerCtx *ctx = (ServerCtx *)(uintptr_t)ctx_val->as.number;
    esu_release(ctx_val);
    return ctx;
}

void esu_http_serve_static(EsuValue *server, EsuValue *dir, EsuValue *prefix) {
    ServerCtx *ctx = server_ctx(server);
    if (!ctx) return;
    esu_free(ctx->static_dir);
    esu_free(ctx->static_prefix);
    ctx->static_dir = esu_strdup(dir->type == VAL_STRING ? dir->as.string : ".");
    if (prefix && prefix->type == VAL_STRING && prefix->as.string[0] != '\0') {
        /* Normalise: ensure leading '/', drop trailing '/' (unless it's just "/") */
        const char *p = prefix->as.string;
        size_t n = strlen(p);
        char buf[256];
        size_t j = 0;
        if (p[0] != '/') buf[j++] = '/';
        for (size_t i = 0; i < n && j < sizeof(buf) - 1; i++) buf[j++] = p[i];
        while (j > 1 && buf[j - 1] == '/') j--;
        buf[j] = '\0';
        ctx->static_prefix = esu_strdup(buf);
    } else {
        ctx->static_prefix = NULL; /* serve at root */
    }
}

void esu_http_set_before_hook(EsuValue *server, EsuValue *handler) {
    ServerCtx *ctx = server_ctx(server);
    if (!ctx) return;
    if (ctx->before_hook) esu_release(ctx->before_hook);
    ctx->before_hook = esu_retain(handler);
}

void esu_http_add_route(EsuValue *server, EsuValue *method,
                        EsuValue *path, EsuValue *handler) {
    ServerCtx *ctx = server_ctx(server);
    if (!ctx) return;

    if (ctx->route_count >= ctx->route_capacity) {
        ctx->route_capacity = ctx->route_capacity == 0 ? 8 : ctx->route_capacity * 2;
        ctx->routes = esu_realloc(ctx->routes, sizeof(Route) * ctx->route_capacity);
    }

    ctx->routes[ctx->route_count].method = esu_strdup(
        method->type == VAL_STRING ? method->as.string : "GET");
    ctx->routes[ctx->route_count].path = esu_strdup(
        path->type == VAL_STRING ? path->as.string : "/");
    ctx->routes[ctx->route_count].handler = esu_retain(handler);
    ctx->route_count++;
}

/* Internal: append a route record with one of the two handler kinds set. */
static void add_ws_route_internal(ServerCtx *ctx, const char *path,
                                  EsuValue *connect_handler,
                                  EsuValue *message_handler) {
    if (ctx->ws_route_count >= ctx->ws_route_capacity) {
        ctx->ws_route_capacity = ctx->ws_route_capacity == 0 ? 4 : ctx->ws_route_capacity * 2;
        ctx->ws_routes = esu_realloc(ctx->ws_routes, sizeof(WsRoute) * ctx->ws_route_capacity);
    }
    ctx->ws_routes[ctx->ws_route_count].path = esu_strdup(path);
    ctx->ws_routes[ctx->ws_route_count].connect_handler =
        connect_handler ? esu_retain(connect_handler) : NULL;
    ctx->ws_routes[ctx->ws_route_count].message_handler =
        message_handler ? esu_retain(message_handler) : NULL;
    ctx->ws_route_count++;
}

/* Register a WS route with a connect-handler. The handler is called once per
 * new connection and typically uses __ws_on_message / __ws_on_close to set
 * per-connection state. */
void esu_http_add_ws_route(EsuValue *server, EsuValue *path, EsuValue *connect_handler) {
    ServerCtx *ctx = server_ctx(server);
    if (!ctx) return;
    add_ws_route_internal(ctx,
        path->type == VAL_STRING ? path->as.string : "/",
        connect_handler, NULL);
}

/* Register a WS route with a direct message handler. Each new connection on
 * the route gets this handler installed as its on-message; no connect-time
 * code runs. The handler is called as (connection, message). */
void esu_http_add_ws_handler(EsuValue *server, EsuValue *path, EsuValue *message_handler) {
    ServerCtx *ctx = server_ctx(server);
    if (!ctx) return;
    add_ws_route_internal(ctx,
        path->type == VAL_STRING ? path->as.string : "/",
        NULL, message_handler);
}

/* Install a per-connection message handler. Called from inside a connect
 * handler — uses active_ws_conn. */
void esu_ws_on_message(EsuValue *handler) {
    if (!active_ws_conn) {
        esu_raise_msg("'on message' must be called inside a 'when someone connects' handler");
        return;
    }
    if (active_ws_conn->message_handler) esu_release(active_ws_conn->message_handler);
    active_ws_conn->message_handler = esu_retain(handler);
}

void esu_ws_on_close(EsuValue *handler) {
    if (!active_ws_conn) {
        esu_raise_msg("'on close' must be called inside a 'when someone connects' handler");
        return;
    }
    if (active_ws_conn->close_handler) esu_release(active_ws_conn->close_handler);
    active_ws_conn->close_handler = esu_retain(handler);
}

/* Send a text frame.
 *
 *   - If `connection` is a server-side connection value (has __conn), this is
 *     a broadcast-to-one over the *server's* mongoose conn.
 *   - If `connection` is a client-side WS handle (has __client), this delegates
 *     to esu_ws_client_send so the client's mgr gets polled to flush.
 *   - If `connection` is NULL, use the currently-dispatching server connection.
 */
void esu_ws_send(EsuValue *connection, EsuValue *message) {
    /* Client-handle dispatch */
    if (connection && connection->type == VAL_MAP) {
        EsuValue *p = esu_map_get(connection, esu_string("__client"));
        if (p && p->type == VAL_NUMBER && p->as.number != 0) {
            esu_release(p);
            esu_ws_client_send(connection, message);
            return;
        }
        if (p) esu_release(p);
    }
    struct mg_connection *c = connection ? conn_from_value(connection)
                                         : (active_ws_conn ? active_ws_conn->c : NULL);
    if (!c) { esu_raise_msg("no connection to send to"); return; }
    char *msg = esu_to_cstring(message);
    mg_ws_send(c, msg, strlen(msg), WEBSOCKET_OP_TEXT);
    esu_free(msg);
}

/* Send to every active WS connection on the current server. */
void esu_ws_broadcast(EsuValue *message) {
    if (!active_server) { esu_raise_msg("broadcast called outside a server context"); return; }
    char *msg = esu_to_cstring(message);
    size_t mlen = strlen(msg);
    for (int i = 0; i < active_server->ws_conn_count; i++) {
        mg_ws_send(active_server->ws_conns[i]->c, msg, mlen, WEBSOCKET_OP_TEXT);
    }
    esu_free(msg);
}

/* Close a WS connection. Dispatches to the client-close path if `connection`
 * is a WS client handle (has __client). NULL → close the active server conn. */
void esu_ws_close(EsuValue *connection) {
    if (connection && connection->type == VAL_MAP) {
        EsuValue *p = esu_map_get(connection, esu_string("__client"));
        if (p && p->type == VAL_NUMBER && p->as.number != 0) {
            esu_release(p);
            esu_ws_client_close(connection);
            return;
        }
        if (p) esu_release(p);
    }
    struct mg_connection *c = connection ? conn_from_value(connection)
                                         : (active_ws_conn ? active_ws_conn->c : NULL);
    if (!c) return;
    c->is_closing = 1;
}

EsuValue *esu_http_listen(EsuValue *server) {
    ServerCtx *ctx = server_ctx(server);
    if (!ctx) {
        esu_raise_msg("invalid server object");
        return esu_nothing();
    }
    active_server = ctx;

    struct mg_mgr mgr;
    mg_mgr_init(&mgr);

    char listen_url[64];
    snprintf(listen_url, sizeof(listen_url), "http://0.0.0.0:%d", ctx->port);

    struct mg_connection *c = mg_http_listen(&mgr, listen_url, server_fn, ctx);
    if (!c) {
        mg_mgr_free(&mgr);
        char msg[128];
        snprintf(msg, sizeof(msg), "cannot listen on port %d", ctx->port);
        esu_raise_msg(msg);
        return esu_nothing();
    }

    printf("Esu server listening on port %d\n", ctx->port);
    fflush(stdout);

    /* Event loop — blocks forever */
    for (;;) mg_mgr_poll(&mgr, 1000);

    mg_mgr_free(&mgr);
    return esu_nothing();
}

/* Response builder */
EsuValue *esu_http_respond(EsuValue *status, EsuValue *body) {
    EsuValue *response = esu_map_new();
    esu_map_set(response, esu_string("status"), esu_retain(status));
    esu_map_set(response, esu_string("body"), esu_retain(body));
    return response;
}

/* Redirect response: { status: 302|301, headers: { Location: url } }.
 * `permanent` truthy → 301, else 302. */
EsuValue *esu_http_redirect(EsuValue *url, EsuValue *permanent) {
    int perm = permanent && ((permanent->type == VAL_BOOL && permanent->as.boolean) ||
                             (permanent->type == VAL_NUMBER && permanent->as.number != 0));
    EsuValue *response = esu_map_new();
    EsuValue *status = esu_number(perm ? 301 : 302);
    EsuValue *headers = esu_map_new();
    EsuValue *loc_key = esu_string("Location");
    EsuValue *loc_val = (url->type == VAL_STRING) ? esu_retain(url) : esu_string("/");
    esu_map_set(headers, loc_key, loc_val);
    esu_map_set(response, esu_string("status"), status);
    esu_map_set(response, esu_string("headers"), headers);
    esu_map_set(response, esu_string("body"), esu_string(""));
    esu_release(status);
    esu_release(headers);
    esu_release(loc_key);
    esu_release(loc_val);
    return response;
}

/* Request accessors */
EsuValue *esu_http_req_path(EsuValue *request) {
    return esu_map_get(request, esu_string("path"));
}

EsuValue *esu_http_req_method(EsuValue *request) {
    return esu_map_get(request, esu_string("method"));
}

EsuValue *esu_http_req_body(EsuValue *request) {
    return esu_map_get(request, esu_string("body"));
}

EsuValue *esu_http_req_header(EsuValue *request, EsuValue *name) {
    EsuValue *headers = esu_map_get(request, esu_string("headers"));
    if (!headers || headers->type != VAL_MAP) {
        if (headers) esu_release(headers);
        return esu_string("");
    }
    /* Case-insensitive lookup */
    if (name->type != VAL_STRING) {
        esu_release(headers);
        return esu_string("");
    }
    EsuMap *map = headers->as.map;
    for (int i = 0; i < map->count; i++) {
        if (strcasecmp(map->entries[i].key, name->as.string) == 0) {
            EsuValue *v = esu_retain(map->entries[i].value);
            esu_release(headers);
            return v;
        }
    }
    esu_release(headers);
    return esu_string("");
}

EsuValue *esu_http_req_query(EsuValue *request, EsuValue *name) {
    EsuValue *query = esu_map_get(request, esu_string("query"));
    if (!query || query->type != VAL_MAP) {
        if (query) esu_release(query);
        return esu_string("");
    }
    EsuValue *v = esu_map_get(query, name);
    esu_release(query);
    if (!v || v->type == VAL_NOTHING) {
        if (v) esu_release(v);
        return esu_string("");
    }
    return v;
}

EsuValue *esu_http_req_route_param(EsuValue *request, EsuValue *name) {
    EsuValue *params = esu_map_get(request, esu_string("params"));
    if (!params || params->type != VAL_MAP) {
        if (params) esu_release(params);
        return esu_string("");
    }
    EsuValue *v = esu_map_get(params, name);
    esu_release(params);
    if (!v || v->type == VAL_NOTHING) {
        if (v) esu_release(v);
        return esu_string("");
    }
    return v;
}

EsuValue *esu_http_req_cookie(EsuValue *request, EsuValue *name) {
    EsuValue *cookies = esu_map_get(request, esu_string("cookies"));
    if (!cookies || cookies->type != VAL_MAP) {
        if (cookies) esu_release(cookies);
        return esu_string("");
    }
    EsuValue *v = esu_map_get(cookies, name);
    esu_release(cookies);
    if (!v || v->type == VAL_NOTHING) {
        if (v) esu_release(v);
        return esu_string("");
    }
    return v;
}
