#include "esu_http.h"
#include "esu_json.h"
#include "esu_map.h"
#include "esu_list.h"
#include "esu_mem.h"
#include "esu_error.h"
#include "mongoose.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ===== HTTP CLIENT ===== */

/* Shared state for client requests */
typedef struct {
    char *response_body;
    int response_len;
    int done;
    int status;
} ClientCtx;

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
        ctx->done = 1;
        c->is_closing = 1;
    } else if (ev == MG_EV_ERROR) {
        ctx->done = 1;
        c->is_closing = 1;
    }
}

static EsuValue *http_request(const char *url, const char *method,
                              const char *body, const char *content_type) {
    struct mg_mgr mgr;
    ClientCtx ctx = {NULL, 0, 0, 0};
    mg_mgr_init(&mgr);

    struct mg_str host = mg_url_host(url);
    char headers[512] = "";
    if (content_type) {
        snprintf(headers, sizeof(headers),
                 "Content-Type: %s\r\n", content_type);
    }

    struct mg_connection *c = mg_http_connect(&mgr, url, client_fn, &ctx);
    if (!c) {
        mg_mgr_free(&mgr);
        esu_raise_msg("cannot connect to URL");
        return esu_nothing();
    }

    /* Build and send request */
    int is_ssl = (strncmp(url, "https", 5) == 0);
    if (is_ssl) {
        /* mongoose needs TLS config for HTTPS */
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

    /* Poll until done */
    while (!ctx.done) mg_mgr_poll(&mgr, 1000);

    mg_mgr_free(&mgr);

    if (ctx.response_body) {
        EsuValue *result = esu_map_new();
        EsuValue *body_val = esu_string(ctx.response_body);
        EsuValue *status_val = esu_number(ctx.status);
        esu_map_set(result, esu_string("body"), body_val);
        esu_map_set(result, esu_string("status"), status_val);
        esu_release(body_val);
        esu_release(status_val);
        esu_free(ctx.response_body);
        return result;
    }
    return esu_nothing();
}

EsuValue *esu_http_get(EsuValue *url) {
    if (url->type != VAL_STRING) {
        esu_raise_msg("URL must be a string");
        return esu_nothing();
    }
    return http_request(url->as.string, "GET", NULL, NULL);
}

EsuValue *esu_http_post(EsuValue *url, EsuValue *body) {
    if (url->type != VAL_STRING) {
        esu_raise_msg("URL must be a string");
        return esu_nothing();
    }
    char *body_str = esu_to_cstring(body);
    EsuValue *result = http_request(url->as.string, "POST", body_str,
                                    "application/json");
    esu_free(body_str);
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

    /* Route params placeholder — populated by server_fn after route match */
    EsuValue *params = esu_map_new();
    esu_map_set(req, esu_string("params"), params);
    esu_release(params);

    return req;
}

/* Build "Header-Name: value\r\n..." string from a headers map.
 * Caller frees. Always includes a trailing CRLF chunk per header. */
static char *build_response_headers(EsuValue *headers_map, int *has_content_type) {
    *has_content_type = 0;
    if (!headers_map || headers_map->type != VAL_MAP) {
        return esu_strdup("");
    }
    EsuMap *map = headers_map->as.map;
    /* Estimate buffer */
    size_t cap = 256;
    char *buf = esu_alloc(cap);
    buf[0] = '\0';
    size_t len = 0;
    for (int i = 0; i < map->count; i++) {
        char *vstr = esu_to_cstring(map->entries[i].value);
        size_t need = strlen(map->entries[i].key) + strlen(vstr) + 8;
        while (len + need >= cap) {
            cap *= 2;
            buf = esu_realloc(buf, cap);
        }
        if (strcasecmp(map->entries[i].key, "Content-Type") == 0) {
            *has_content_type = 1;
        }
        int written = snprintf(buf + len, cap - len, "%s: %s\r\n",
                               map->entries[i].key, vstr);
        len += (size_t)written;
        esu_free(vstr);
    }
    return buf;
}

static void server_fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev != MG_EV_HTTP_MSG) return;
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;
    ServerCtx *ctx = (ServerCtx *)c->fn_data;

    /* URI as null-terminated for our matcher */
    char uri_buf[2048];
    snprintf(uri_buf, sizeof(uri_buf), "%.*s", (int)hm->uri.len, hm->uri.buf);

    /* Find matching route */
    for (int i = 0; i < ctx->route_count; i++) {
        Route *r = &ctx->routes[i];
        /* Match method (or "*" for any) */
        int method_match = (strcmp(r->method, "*") == 0) ||
            (mg_strcmp(hm->method, mg_str(r->method)) == 0);
        if (!method_match) continue;

        /* Try our placeholder-aware matcher first; fall back to mg_match for *,# */
        EsuValue *route_params = match_route(r->path, uri_buf);
        int matched = (route_params != NULL);
        if (!matched) {
            if (mg_match(hm->uri, mg_str(r->path), NULL)) {
                matched = 1;
                route_params = esu_map_new();
            }
        }

        if (matched) {
            EsuValue *request = build_request(hm);
            esu_map_set(request, esu_string("params"), route_params);
            esu_release(route_params);

            EsuValue *args[1] = { request };
            EsuValue *response = esu_call(r->handler, args, 1);

            int code = 200;
            char *body_str = NULL;
            char *extra_headers = esu_strdup("");
            int has_content_type = 0;

            if (response && response->type == VAL_MAP) {
                EsuValue *status = esu_map_get(response, esu_string("status"));
                EsuValue *body = esu_map_get(response, esu_string("body"));
                EsuValue *headers = esu_map_get(response, esu_string("headers"));
                if (status && status->type == VAL_NUMBER) {
                    code = (int)status->as.number;
                }
                body_str = body ? esu_to_cstring(body) : esu_strdup("");
                esu_free(extra_headers);
                extra_headers = build_response_headers(headers, &has_content_type);
                if (status) esu_release(status);
                if (body) esu_release(body);
                if (headers) esu_release(headers);
            } else if (response && response->type == VAL_STRING) {
                body_str = esu_strdup(response->as.string);
            } else {
                body_str = esu_strdup("");
            }

            /* If user didn't set Content-Type, default to text/plain */
            char headers_with_default[2048];
            if (has_content_type) {
                snprintf(headers_with_default, sizeof(headers_with_default),
                         "%s", extra_headers);
            } else {
                snprintf(headers_with_default, sizeof(headers_with_default),
                         "Content-Type: text/plain\r\n%s", extra_headers);
            }
            mg_http_reply(c, code, headers_with_default, "%s", body_str);

            esu_free(body_str);
            esu_free(extra_headers);
            if (response) esu_release(response);
            esu_release(request);
            return;
        }
    }

    /* No matching route */
    mg_http_reply(c, 404, "", "Not Found");
}

EsuValue *esu_http_create_server(EsuValue *port) {
    int p = (port->type == VAL_NUMBER) ? (int)port->as.number : 8080;
    ServerCtx *ctx = esu_alloc(sizeof(ServerCtx));
    ctx->routes = NULL;
    ctx->route_count = 0;
    ctx->route_capacity = 0;
    ctx->port = p;

    /* Store server context as a map with a hidden pointer */
    EsuValue *server = esu_map_new();
    esu_map_set(server, esu_string("port"), esu_number(p));
    esu_map_set(server, esu_string("__ctx"), esu_number((double)(uintptr_t)ctx));
    return server;
}

void esu_http_add_route(EsuValue *server, EsuValue *method,
                        EsuValue *path, EsuValue *handler) {
    /* Get server context */
    EsuValue *ctx_val = esu_map_get(server, esu_string("__ctx"));
    if (!ctx_val || ctx_val->type != VAL_NUMBER) return;
    ServerCtx *ctx = (ServerCtx *)(uintptr_t)ctx_val->as.number;
    esu_release(ctx_val);

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
    EsuValue *ctx_val = esu_map_get(server, esu_string("__ctx"));
    if (!ctx_val || ctx_val->type != VAL_NUMBER) {
        esu_raise_msg("invalid server object");
        return esu_nothing();
    }
    ServerCtx *ctx = (ServerCtx *)(uintptr_t)ctx_val->as.number;
    esu_release(ctx_val);
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
