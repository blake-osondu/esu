#ifndef ESU_HTTP_H
#define ESU_HTTP_H

#include "esu_value.h"

/* HTTP client: convenience wrappers */
EsuValue *esu_http_get(EsuValue *url);
EsuValue *esu_http_post(EsuValue *url, EsuValue *body);
EsuValue *esu_http_put(EsuValue *url, EsuValue *body);
EsuValue *esu_http_patch(EsuValue *url, EsuValue *body);
EsuValue *esu_http_delete(EsuValue *url);

/* General request: method + URL + options map (body, headers, json) */
EsuValue *esu_http_request(EsuValue *method, EsuValue *url, EsuValue *options);

/* HTTP server */
EsuValue *esu_http_create_server(EsuValue *port);
void esu_http_add_route(EsuValue *server, EsuValue *method,
                        EsuValue *path, EsuValue *handler);
void esu_http_serve_static(EsuValue *server, EsuValue *dir, EsuValue *prefix);
void esu_http_set_before_hook(EsuValue *server, EsuValue *handler);
EsuValue *esu_http_listen(EsuValue *server);

/* Response builders */
EsuValue *esu_http_respond(EsuValue *status, EsuValue *body);
EsuValue *esu_http_redirect(EsuValue *url, EsuValue *permanent);

/* Request accessors */
EsuValue *esu_http_req_path(EsuValue *request);
EsuValue *esu_http_req_method(EsuValue *request);
EsuValue *esu_http_req_body(EsuValue *request);
EsuValue *esu_http_req_header(EsuValue *request, EsuValue *name);
EsuValue *esu_http_req_query(EsuValue *request, EsuValue *name);
EsuValue *esu_http_req_route_param(EsuValue *request, EsuValue *name);
EsuValue *esu_http_req_cookie(EsuValue *request, EsuValue *name);

#endif
