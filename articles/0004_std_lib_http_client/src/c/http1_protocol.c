#include <httpc/http1_protocol.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static Error growable_buffer_append(Http1Protocol* self, GrowableBuffer* buf, const void* data, size_t len) {
    if (buf->len + len > buf->capacity) {
        size_t new_capacity = buf->capacity == 0 ? 2048 : buf->capacity * 2;
        while (new_capacity < buf->len + len) {
            new_capacity *= 2;
        }
        char* new_data = self->syscalls->realloc(buf->data, new_capacity);
        if (!new_data) {
            return (Error){ErrorType.HTTPC, HttpClientErrorCode.HTTP_PARSE_FAILURE};
        }
        buf->data = new_data;
        buf->capacity = new_capacity;
    }
    self->syscalls->memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    return (Error){ErrorType.NONE, 0};
}

static void http_response_parse_headers(Http1Protocol* self, HttpResponse* res, char* headers_start) {
    char* saveptr;
    char* line = self->syscalls->strtok_r(headers_start, "\r\n", &saveptr);

    if (!line) return;

    self->syscalls->sscanf(line, "HTTP/1.1 %d", &res->status_code);
    res->status_message = self->syscalls->strchr(line, ' ') + 1;
    if (res->status_message) {
        res->status_message = self->syscalls->strchr(res->status_message, ' ') + 1;
    }

    res->num_headers = 0;
    while ((line = self->syscalls->strtok_r(nullptr, "\r\n", &saveptr)) != nullptr) {
        if (res->num_headers >= 32) break;

        char* colon = self->syscalls->strchr(line, ':');
        if (colon) {
            *colon = '\0';
            res->headers[res->num_headers].key = line;
            char* value = colon + 1;
            while (*value == ' ') value++;
            res->headers[res->num_headers].value = value;
            res->num_headers++;
        }
    }
}

static Error build_request_in_buffer(Http1Protocol* self, const HttpRequest* request) {
    Error err = {ErrorType.NONE, 0};
    self->buffer.len = 0;

    const char* method_str = request->method == HTTP_GET ? "GET" : "POST";
    char request_line[2048];
    int request_line_len = self->syscalls->snprintf(request_line, sizeof(request_line), "%s %s HTTP/1.1\r\n", method_str, request->path);
    err = growable_buffer_append(self, &self->buffer, request_line, request_line_len);
    if (err.type != ErrorType.NONE) return err;

    for (size_t i = 0; i < request->num_headers; ++i) {
        char header_line[1024];
        int len = self->syscalls->snprintf(header_line, sizeof(header_line), "%s: %s\r\n", request->headers[i].key, request->headers[i].value);
        err = growable_buffer_append(self, &self->buffer, header_line, len);
        if (err.type != ErrorType.NONE) return err;
    }
    err = growable_buffer_append(self, &self->buffer, "\r\n", 2);
    if (err.type != ErrorType.NONE) return err;


    if (request->body) {
        err = growable_buffer_append(self, &self->buffer, request->body, self->syscalls->strlen(request->body));
        if (err.type != ErrorType.NONE) return err;
    }

    return err;
}

static Error read_and_parse_response(Http1Protocol* self, HttpResponse* response) {
    Error err = {ErrorType.NONE, 0};
    self->buffer.len = 0;

    int content_length = -1;
    size_t header_len = 0;
    bool headers_parsed = false;

    while(1) {
        ssize_t bytes_read = 0;
        err = self->transport->read(self->transport->context, self->buffer.data + self->buffer.len, self->buffer.capacity - self->buffer.len, &bytes_read);
        if (err.type != ErrorType.NONE && err.code != TransportErrorCode.CONNECTION_CLOSED) {
            return err;
        }
        self->buffer.len += bytes_read;

        if (!headers_parsed) {
            char* header_end = self->syscalls->strstr(self->buffer.data, "\r\n\r\n");
            if (header_end) {
                headers_parsed = true;
                header_len = (header_end - self->buffer.data) + 4;
                http_response_parse_headers(self, response, self->buffer.data);

                for (size_t i = 0; i < response->num_headers; ++i) {
                    if (self->syscalls->strcasecmp(response->headers[i].key, "Content-Length") == 0) {
                        content_length = self->syscalls->atoi(response->headers[i].value);
                        break;
                    }
                }
            }
        }

        if (headers_parsed) {
            if (content_length != -1) {
                size_t total_size = header_len + content_length;
                if (self->buffer.capacity < total_size + 1) {
                    char* new_data = self->syscalls->realloc(self->buffer.data, total_size + 1);
                    if (!new_data) {
                        return (Error){ErrorType.HTTPC, HttpClientErrorCode.HTTP_PARSE_FAILURE};
                    }
                    self->buffer.data = new_data;
                    self->buffer.capacity = total_size + 1;
                }
                if (self->buffer.len >= total_size) {
                    response->body = self->buffer.data + header_len;
                    response->body_len = content_length;
                    self->buffer.data[total_size] = '\0';
                    break;
                }
            } else if (err.code == TransportErrorCode.CONNECTION_CLOSED) {
                response->body = self->buffer.data + header_len;
                response->body_len = self->buffer.len - header_len;
                self->buffer.data[self->buffer.len] = '\0';
                break;
            }
        }

        if (err.code == TransportErrorCode.CONNECTION_CLOSED) {
            break;
        }
    }

    if (!headers_parsed && self->buffer.len > 0) {
        return (Error){ErrorType.HTTPC, HttpClientErrorCode.HTTP_PARSE_FAILURE};
    }

    return (Error){ErrorType.NONE, 0};
}

static Error http1_protocol_perform_request(void* context,
                                            const HttpRequest* request,
                                            HttpResponse* response) {
    Http1Protocol* self = (Http1Protocol*)context;
    Error err = {ErrorType.NONE, 0};
    ssize_t bytes_written = 0;

    err = build_request_in_buffer(self, request);
    if (err.type != ErrorType.NONE) {
        return err;
    }

    err = self->transport->write(self->transport->context, self->buffer.data, self->buffer.len, &bytes_written);
    if (err.type != ErrorType.NONE) {
        return err;
    }

    return read_and_parse_response(self, response);
}


static Error http1_protocol_connect(void* context, const char* host, int port) {
    Http1Protocol* self = (Http1Protocol*)context;
    return self->transport->connect(self->transport->context, host, port);
}

static Error http1_protocol_disconnect(void* context) {
    Http1Protocol* self = (Http1Protocol*)context;
    return self->transport->close(self->transport->context);
}

HttpProtocolInterface* http1_protocol_new(TransportInterface* transport, const HttpcSyscalls* syscalls_override) {
    if (!default_syscalls_initialized) {
        httpc_syscalls_init_default(&DEFAULT_SYSCALLS);
        default_syscalls_initialized = 1;
    }

    if (!syscalls_override)
    {
        syscalls_override = &DEFAULT_SYSCALLS;
    }

    Http1Protocol* self = syscalls_override->malloc(sizeof(Http1Protocol));
    if (!self) {
        return nullptr;
    }

    syscalls_override->memset(self, 0, sizeof(Http1Protocol));
    self->syscalls = syscalls_override;

    self->transport = transport;
    self->interface.context = self;
    self->interface.transport = transport;
    self->interface.connect = http1_protocol_connect;
    self->interface.disconnect = http1_protocol_disconnect;
    self->interface.perform_request = http1_protocol_perform_request;

    return &self->interface;
}

void http1_protocol_destroy(HttpProtocolInterface* protocol) {
    if (!protocol) {
        return;
    }
    Http1Protocol* self = (Http1Protocol*)protocol;
    self->syscalls->free(self);
}