#pragma once

#include "http.h"

void panic(char *msg);
void free_callback(int err, void *udata);
void generate_request(char **buffer, size_t *size, uri_t uri);
bool ci_memcmp(char *s1, char *s2, size_t n);
bool mem_compare_trimed(char *a, size_t a_size, char *b, size_t b_size);
struct addrinfo* resolve_address(char *hostname, char *port);
size_t parse_size_t_trimmed(char *str, size_t size, bool *succ);

