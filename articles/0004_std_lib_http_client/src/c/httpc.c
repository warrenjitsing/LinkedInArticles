#include <string.h>
#include <stdio.h>

#include <httpc/httpc.h>
#include <httpc/tcp_transport.h>
#include <httpc/unix_transport.h>
#include <httpc/http1_protocol.h>


static Error http_client_get(struct HttpClient* self,
                             const char* path,
                             HttpResponse* response) {
    HttpRequest request = {};
    request.method = HTTP_GET;
    request.path = path;

    return self->protocol->perform_request(self->protocol->context, &request, response);
}

static Error http_client_post(struct HttpClient* self,
                              const char* path,
                              const char* body,
                              HttpResponse* response) {
    HttpRequest request = {};
    request.method = HTTP_POST;
    request.path = path;
    request.body = body;

    return self->protocol->perform_request(self->protocol->context, &request, response);
}

Error http_client_init_with_protocol(struct HttpClient* self, HttpProtocolInterface* protocol, HttpResponseMemoryPolicy policy) {
    self->protocol = protocol;
    self->get = http_client_get;
    self->post = http_client_post;
    return (Error){ErrorType.NONE, 0};
}

Error http_client_init(struct HttpClient* self, int transport_type, int protocol_type, HttpResponseMemoryPolicy policy) {
    TransportInterface* transport = NULL;
    if (transport_type == HttpTransportType.TCP) {
        transport = tcp_transport_new(NULL);
    } else if (transport_type == HttpTransportType.UNIX) {
        transport = unix_transport_new(NULL);
    }

    if (!transport) {
        return (Error){ErrorType.TRANSPORT, TransportErrorCode.INIT_FAILURE};
    }

    HttpProtocolInterface* protocol = NULL;
    if (protocol_type == HttpProtocolType.HTTP1) {
        protocol = http1_protocol_new(transport, NULL, policy);
    }

    if (!protocol) {
        // Cleanup the transport if protocol creation failed
        transport->destroy(transport->context);
        return (Error){ErrorType.HTTPC, HttpClientErrorCode.INIT_FAILURE};
    }

    return http_client_init_with_protocol(self, protocol, policy);
}

void http_client_destroy(struct HttpClient* self) {
    if (!self || !self->protocol) {
        return;
    }

    TransportInterface* transport = self->protocol->transport;

    http1_protocol_destroy(self->protocol);
    transport->destroy(transport->context);
}