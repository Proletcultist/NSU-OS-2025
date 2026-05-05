#pragma once

#include "http.h"

void generate_request(char **buffer, size_t *size, uri_t uri);
void schedule_error_response(int fd, char *msg, size_t msg_size);
bool mem_compare_trimed(char *a, size_t a_size, char *b, size_t b_size);
struct addrinfo* resolve_address(char *hostname, char *port);

