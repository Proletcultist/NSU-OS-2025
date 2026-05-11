#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <stddef.h>
#include "proxy/proxy.h"

int parse_in_port(const char *str, in_port_t *out_port) {
    char *endptr;
    errno = 0;
    unsigned long val = strtoul(str, &endptr, 10);

    if (endptr == str || *endptr != '\0' || errno == ERANGE || val > 65535 || str[0] == '-') {
        return -1;
    }

    *out_port = htons((in_port_t)val);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "[Error] Required ip and port arguments\n");
        return -1;
    }

    struct in_addr ip;
    in_port_t port;

    if (inet_pton(AF_INET, argv[1], &ip) != 1) {
        fprintf(stderr, "[Error] Failed to parse \"%s\" to IPv4 address\n", argv[1]);
        return -1;
    }
    if (parse_in_port(argv[2], &port)) {
        fprintf(stderr, "[Error] Failed to parse \"%s\" to port number\n", argv[2]);
        return -1;
    }

    if (start_proxy(ip, port)) {
        return -1;
    }

    return 0;
}
