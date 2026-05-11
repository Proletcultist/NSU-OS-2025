#pragma once

#include <netinet/in.h>
#include <arpa/inet.h>

int start_proxy(struct in_addr ip, in_port_t port);
