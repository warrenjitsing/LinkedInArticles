#pragma once

#include <httpc/transport.h>

typedef enum {
    HTTP_GET,
    HTTP_POST
} HttpMethod;

typedef struct {
    const char* key;
    const char* value;
} HttpHeader;

typedef struct {
    HttpMethod method;
    const char* path;
    const char* body;
    HttpHeader headers[32];
    size_t num_headers;
} HttpRequest;

typedef enum {
    HTTP_RESPONSE_UNSAFE_ZERO_COPY,
    HTTP_RESPONSE_SAFE_OWNING,
} HttpResponseMemoryPolicy;

typedef struct {
    int status_code;
    const char* status_message;
    const char* body;
    size_t body_len;
    HttpHeader headers[32];
    size_t num_headers;
    void* _owned_buffer;
} HttpResponse;

void http_response_destroy(HttpResponse* response);

typedef struct {
    void* context;
    TransportInterface* transport;
    Error (*connect)(void* context, const char* host, int port);
    Error (*disconnect)(void* context);
    Error (*perform_request)(void* context,
                             const HttpRequest* request,
                             HttpResponse* response);
    void (*destroy)(void* context);
} HttpProtocolInterface;

static const struct {
    const int CONTINUE_100;
    const int OK_200;
    const int CREATED_201;
    const int ACCEPTED_202;
    const int FOUND_302;
    const int BAD_REQUEST_400;
    const int UNAUTHORIZED_401;
    const int FORBIDDEN_403;
    const int NOT_FOUND_404;
    const int INTERNAL_SERVER_ERROR_500;
    const int BAD_GATEWAY_502;
} HttpStatusCode = {
    .CONTINUE_100 = 100,
    .OK_200 = 200,
    .CREATED_201 = 201,
    .ACCEPTED_202 = 202,
    .FOUND_302 = 302,
    .BAD_REQUEST_400 = 400,
    .UNAUTHORIZED_401 = 401,
    .FORBIDDEN_403 = 403,
    .NOT_FOUND_404 = 404,
    .INTERNAL_SERVER_ERROR_500 = 500,
    .BAD_GATEWAY_502 = 502,
};