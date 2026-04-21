#include <stddef.h>
#include "proxy/responses.h"

char bad_request_response[] = "HTTP/1.0 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
size_t bad_request_response_size = sizeof(bad_request_response);
