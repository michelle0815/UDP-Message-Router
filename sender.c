#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "message.h"

static int sleep_ms(uint32_t milliseconds)
{
    struct timespec remaining;
    remaining.tv_sec = (time_t)(milliseconds / 1000U);
    remaining.tv_nsec = (long)((milliseconds % 1000U) * 1000000U);
    while (nanosleep(&remaining, &remaining) != 0) {
        if (errno != EINTR) {
            (void)fprintf(stderr, "nanosleep: %s\n", strerror(errno));
            return -1;
        }
    }
    return 0;
}

static int send_test_packet(int socket_fd,
                            const struct sockaddr_in *destination,
                            uint32_t app_id,
                            uint32_t command_id)
{
    uint32_t words[MSG_WORD_COUNT];
    size_t index;
    ssize_t sent;

    words[0] = htonl(app_id);
    words[1] = htonl(command_id);
    for (index = 0U; index < MSG_PARAM_COUNT; index++) {
        words[index + 2U] = htonl((uint32_t)index);
    }
    do {
        sent = sendto(socket_fd, words, sizeof(words), 0,
                      (const struct sockaddr *)destination,
                      (socklen_t)sizeof(*destination));
    } while ((sent < 0) && (errno == EINTR));

    if (sent != (ssize_t)sizeof(words)) {
        (void)fprintf(stderr, "sendto: %s\n", (sent < 0) ? strerror(errno) : "short datagram");
        return -1;
    }
    return 0;
}

static int run_test(int socket_fd,
                    const struct sockaddr_in *destination,
                    const char *description,
                    uint32_t app_id,
                    uint32_t command_id,
                    uint32_t delay_ms)
{
    (void)printf("%s\n", description);
    if (send_test_packet(socket_fd, destination, app_id, command_id) != 0) {
        return -1;
    }
    return sleep_ms(delay_ms);
}

int main(void)
{
    struct sockaddr_in destination;
    int socket_fd;
    int exit_status = 1;

    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        (void)fprintf(stderr, "socket: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    (void)memset(&destination, 0, sizeof(destination));
    destination.sin_family = AF_INET;
    destination.sin_port = htons((uint16_t)UDP_ROUTER_PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &destination.sin_addr) != 1) {
        (void)fprintf(stderr, "inet_pton failed for loopback address\n");
        goto cleanup;
    }

    (void)printf("Sending five router validation cases\n");
    if (run_test(socket_fd, &destination, "Test 1: APP_ID_1 command 101", APP_ID_1, 101U, 1200U) != 0) {
        goto cleanup;
    }
    if (run_test(socket_fd, &destination, "Test 2: APP_ID_2 command 202", APP_ID_2, 202U, 1200U) != 0) {
        goto cleanup;
    }
    if (run_test(socket_fd, &destination, "Test 3: unsupported app_id 99", 99U, 303U, 1200U) != 0) {
        goto cleanup;
    }
    if (run_test(socket_fd, &destination, "Test 4: APP_ID_1 command 401", APP_ID_1, 401U, 300U) != 0) {
        goto cleanup;
    }
    if (run_test(socket_fd, &destination, "Test 4: APP_ID_1 command 402 inside rate window", APP_ID_1, 402U, 800U) != 0) {
        goto cleanup;
    }
    if (run_test(socket_fd, &destination, "Test 5: APP_ID_2 command 403 after rate window", APP_ID_2, 403U, 500U) != 0) {
        goto cleanup;
    }
    exit_status = 0;

cleanup:
    if (close(socket_fd) != 0) {
        (void)fprintf(stderr, "close: %s\n", strerror(errno));
        exit_status = 1;
    }
    return exit_status;
}
