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

/* ===== HTTP SERVER ===== */

typedef struct {
    char *method;   /* "GET", "POST", etc. */
    char *path;     /* "/hello", "/api/users", etc. */
    EsuValue *handler; /* Closure to call */
} Route;

typedef struct {
    Route *routes;
    int route_count;
    int route_capacity;
    int port;
    char *static_dir;       /* directory to serve static files from, or NULL */
    char *static_prefix;    /* URL prefix the static dir is mounted at, or NULL ("/") */
    EsuValue *before_hook;  /* closure run before every request, or NULL */
} ServerCtx;

static ServerCtx *active_server = NULL;

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

static void server_fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev != MG_EV_HTTP_MSG) return;
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;
    ServerCtx *ctx = (ServerCtx *)c->fn_data;

    /* URI as null-terminated for our matcher */
    char uri_buf[2048];
    snprintf(uri_buf, sizeof(uri_buf), "%.*s", (int)hm->uri.len, hm->uri.buf);

    /* 1. Before-hook (middleware). If it returns a real response, send it and stop. */
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

        /* Placeholder-aware matcher first; fall back to mg_match for *,# */
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
                /* Mounted at a sub-path: strip the prefix so files resolve. */
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
