#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "proxy.h"
#include "cache.h"
#include "exchange_buffer.h"

int main() {
    struct in_addr ip;
    inet_pton(AF_INET, "127.0.0.1", &ip);

    proxy_t p;
    if (proxy_construct(&p, ip, htons(12345))) {
        perror("Failed to construct proxy");
    }

    while (true) {
        proxy_proceed(&p);
    }

    proxy_destruct(&p);

    return 0;
}
