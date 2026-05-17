#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <stddef.h>
#include "proxy/proxy.h"
#include "proxy/util.h"

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

    struct option long_opts[] = {{
                                    .name = "cache-cap",
                                    .has_arg = 1,
                                    .flag = NULL,
                                    .val = 'c'
                                },
                                {
                                    .name = "help",
                                    .has_arg = 0,
                                    .flag = NULL,
                                    .val = 'h'
                                }, {}};

    ssize_t cache_cap = -1;
    int opt;
    while ((opt = getopt_long(argc, argv, "+c:h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'c':
                bool succ;
                cache_cap = parse_ssize_t(optarg, &succ);
                if (!succ) {
                    fprintf(stderr, "Incorrect value of cache capacity. Expected numver from range [-1, SSIZE_MAX], got: \"%s\"\n", optarg);
                    return -1;
                }
                break;
            case 'h':
                fprintf(stdout, "hproxy [options] ip port\nOptions:\n\t-h, --help\t\t\t\tDisplay this information.\n\t-c, --cache-cap <cache_capacity>\tSet cache capacity to <cache_capacity>.\n\t\t\t\t\t\t<cache_capacity> must be a number from range [-1, SSIZE_MAX]\n");
                return 0;
            case '?':
            default:
                return -1;
        }
    }

    if (argc - optind != 2) {
        fprintf(stderr, "[Error] Required ip and port arguments\n");
        return -1;
    }

    struct in_addr ip;
    in_port_t port;

    if (inet_pton(AF_INET, argv[optind], &ip) != 1) {
        fprintf(stderr, "[Error] Failed to parse \"%s\" to IPv4 address\n", argv[optind]);
        return -1;
    }
    if (parse_in_port(argv[optind + 1], &port)) {
        fprintf(stderr, "[Error] Failed to parse \"%s\" to port number\n", argv[optind + 1]);
        return -1;
    }

    if (start_proxy(ip, port, cache_cap)) {
        return -1;
    }

    return 0;
}
