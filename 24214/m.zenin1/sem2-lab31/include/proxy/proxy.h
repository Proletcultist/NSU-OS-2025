#pragma once

#include <netinet/in.h>
#include <arpa/inet.h>

void start_proxy(struct in_addr ip, in_port_t port);
