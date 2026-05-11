#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <stddef.h>
#include "proxy/proxy.h"

int main(int argc, char **argv) {
    struct in_addr ip;
    in_port_t port = htons(12345);
    inet_pton(AF_INET, "127.0.0.1", &ip);

    start_proxy(ip, port);

    return 0;
}
