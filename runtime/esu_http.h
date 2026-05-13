#ifndef ESU_HTTP_H
#define ESU_HTTP_H

#include "esu_value.h"

/* HTTP client: GET/POST/fetch */
EsuValue *esu_http_get(EsuValue *url);
EsuValue *esu_http_post(EsuValue *url, EsuValue *body);

/* HTTP server */
EsuValue *esu_http_create_server(EsuValue *port);
void esu_http_add_route(EsuValue *server, EsuValue *method,
                        EsuValue *path, EsuValue *handler);
EsuValue *esu_http_listen(EsuValue *server);

/* Response builders */
EsuValue *esu_http_respond(EsuValue *status, EsuValue *body);

/* Request accessors */
EsuValue *esu_http_req_path(EsuValue *request);
EsuValue *esu_http_req_method(EsuValue *request);
EsuValue *esu_http_req_body(EsuValue *request);
EsuValue *esu_http_req_header(EsuValue *request, EsuValue *name);
EsuValue *esu_http_req_query(EsuValue *request, EsuValue *name);
EsuValue *esu_http_req_route_param(EsuValue *request, EsuValue *name);

#endif
