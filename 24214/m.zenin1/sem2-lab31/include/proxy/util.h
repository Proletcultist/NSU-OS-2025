#pragma once

#include "http.h"

void free_callback(ssize_t r, int err, void *udata);
void generate_request(char **buffer, size_t *size, uri_t uri);
bool mem_compare_trimed(char *a, size_t a_size, char *b, size_t b_size);
struct addrinfo* resolve_address(char *hostname, char *port);

