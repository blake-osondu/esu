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

    return req;
}

static void server_fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev != MG_EV_HTTP_MSG) return;
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;
    ServerCtx *ctx = (ServerCtx *)c->fn_data;

    /* Find matching route */
    for (int i = 0; i < ctx->route_count; i++) {
        Route *r = &ctx->routes[i];
        /* Match method (or "*" for any) */
        int method_match = (strcmp(r->method, "*") == 0) ||
            (mg_strcmp(hm->method, mg_str(r->method)) == 0);
        /* Match path */
        int path_match = (mg_match(hm->uri, mg_str(r->path), NULL));

        if (method_match && path_match) {
            EsuValue *request = build_request(hm);
            EsuValue *args[1] = { request };
            EsuValue *response = esu_call(r->handler, args, 1);

            /* Response can be a string or a map with status + body */
            if (response && response->type == VAL_MAP) {
                EsuValue *status = esu_map_get(response, esu_string("status"));
                EsuValue *body = esu_map_get(response, esu_string("body"));
                int code = (status && status->type == VAL_NUMBER) ?
                           (int)status->as.number : 200;
                char *body_str = body ? esu_to_cstring(body) : esu_strdup("");
                mg_http_reply(c, code, "Content-Type: text/plain\r\n",
                             "%s", body_str);
                esu_free(body_str);
                if (status) esu_release(status);
                if (body) esu_release(body);
            } else if (response && response->type == VAL_STRING) {
                mg_http_reply(c, 200, "Content-Type: text/plain\r\n",
                             "%s", response->as.string);
            } else {
                mg_http_reply(c, 200, "", "");
            }

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
    (void)request; (void)name;
    /* TODO: parse headers from request */
    return esu_string("");
}
