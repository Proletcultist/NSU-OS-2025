#include <stddef.h>
#include "proxy/responses.h"

char bad_request_response[] = "HTTP/1.0 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
size_t bad_request_response_size = sizeof(bad_request_response);

char too_long_line_response[] = "HTTP/1.0 400 Too long line in request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
size_t too_long_line_response_size = sizeof(too_long_line_response);

char method_not_implemented_response[] = "HTTP/1.0 501 Not Implemented\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
size_t method_not_implemented_response_size = sizeof(method_not_implemented_response);

char method_not_allowed[] = "HTTP/1.0 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n";
size_t method_not_allowed_size = sizeof(method_not_allowed);

char version_not_supported_response[] = "HTTP/1.0 505 HTTP Version Not Supported\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
size_t version_not_supported_response_size = sizeof(version_not_supported_response);

char not_found_response[] = "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
size_t not_found_response_size = sizeof(not_found_response);

char bad_gateway_response[] = "HTTP/1.0 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
size_t bad_gateway_response_size = sizeof(bad_gateway_response);

char gateway_timeout_response[] = "HTTP/1.0 504 Gateway Timeout\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
size_t gateway_timeout_response_size = sizeof(gateway_timeout_response);
