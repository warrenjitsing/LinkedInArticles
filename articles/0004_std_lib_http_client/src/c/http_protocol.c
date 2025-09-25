#include <httpc/http_protocol.h>
#include <stdlib.h>

void http_response_destroy(HttpResponse* response) {
    if (response && response->_owned_buffer) {
        free(response->_owned_buffer);
        response->_owned_buffer = nullptr;
    }
}