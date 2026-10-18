#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "message.h"

typedef struct {
    Message_t entries[APP_QUEUE_CAPACITY];
    size_t head;
    size_t tail;
    size_t count;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} MessageQueue;

typedef struct {
    MessageQueue *queue;
    const char *name;
} AppThreadArgs;

static MessageQueue app1_queue;
static MessageQueue app2_queue;
static volatile sig_atomic_t stop_requested = 0;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static int queue_init(MessageQueue *queue)
{
    int result = pthread_mutex_init(&queue->mutex, NULL);
    if (result != 0) {
        (void)fprintf(stderr, "pthread_mutex_init: %s\n", strerror(result));
        return -1;
    }
    result = pthread_cond_init(&queue->not_empty, NULL);
    if (result != 0) {
        (void)fprintf(stderr, "pthread_cond_init: %s\n", strerror(result));
        (void)pthread_mutex_destroy(&queue->mutex);
        return -1;
    }
    result = pthread_cond_init(&queue->not_full, NULL);
    if (result != 0) {
        (void)fprintf(stderr, "pthread_cond_init: %s\n", strerror(result));
        (void)pthread_cond_destroy(&queue->not_empty);
        (void)pthread_mutex_destroy(&queue->mutex);
        return -1;
    }
    queue->head = 0U;
    queue->tail = 0U;
    queue->count = 0U;
    return 0;
}

static void queue_destroy(MessageQueue *queue)
{
    (void)pthread_cond_destroy(&queue->not_full);
    (void)pthread_cond_destroy(&queue->not_empty);
    (void)pthread_mutex_destroy(&queue->mutex);
}

static int queue_push(MessageQueue *queue, const Message_t *message)
{
    int result = pthread_mutex_lock(&queue->mutex);
    if (result != 0) {
        (void)fprintf(stderr, "pthread_mutex_lock: %s\n", strerror(result));
        return -1;
    }
    while ((queue->count == APP_QUEUE_CAPACITY) && (stop_requested == 0)) {
        result = pthread_cond_wait(&queue->not_full, &queue->mutex);
        if (result != 0) {
            (void)fprintf(stderr, "pthread_cond_wait: %s\n", strerror(result));
            (void)pthread_mutex_unlock(&queue->mutex);
            return -1;
        }
    }
    if (stop_requested != 0) {
        (void)pthread_mutex_unlock(&queue->mutex);
        return 1;
    }
    queue->entries[queue->tail] = *message;
    queue->tail = (queue->tail + 1U) % APP_QUEUE_CAPACITY;
    queue->count++;
    result = pthread_cond_signal(&queue->not_empty);
    if (result != 0) {
        (void)fprintf(stderr, "pthread_cond_signal: %s\n", strerror(result));
        (void)pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    result = pthread_mutex_unlock(&queue->mutex);
    if (result != 0) {
        (void)fprintf(stderr, "pthread_mutex_unlock: %s\n", strerror(result));
        return -1;
    }
    return 0;
}

static int queue_pop(MessageQueue *queue, Message_t *message)
{
    int result = pthread_mutex_lock(&queue->mutex);
    if (result != 0) {
        (void)fprintf(stderr, "pthread_mutex_lock: %s\n", strerror(result));
        return -1;
    }
    while ((queue->count == 0U) && (stop_requested == 0)) {
        result = pthread_cond_wait(&queue->not_empty, &queue->mutex);
        if (result != 0) {
            (void)fprintf(stderr, "pthread_cond_wait: %s\n", strerror(result));
            (void)pthread_mutex_unlock(&queue->mutex);
            return -1;
        }
    }
    if ((queue->count == 0U) && (stop_requested != 0)) {
        (void)pthread_mutex_unlock(&queue->mutex);
        return 1;
    }
    *message = queue->entries[queue->head];
    queue->head = (queue->head + 1U) % APP_QUEUE_CAPACITY;
    queue->count--;
    result = pthread_cond_signal(&queue->not_full);
    if (result != 0) {
        (void)fprintf(stderr, "pthread_cond_signal: %s\n", strerror(result));
        (void)pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    result = pthread_mutex_unlock(&queue->mutex);
    if (result != 0) {
        (void)fprintf(stderr, "pthread_mutex_unlock: %s\n", strerror(result));
        return -1;
    }
    return 0;
}

static void print_message(const char *name, const Message_t *message)
{
    size_t index;
    (void)printf("%s: app_id=%" PRIu32 ", command_id=%" PRIu32 ", params=[",
                 name, message->app_id, message->command_id);
    for (index = 0U; index < MSG_PARAM_COUNT; index++) {
        (void)printf("%s%" PRIu32, (index == 0U) ? "" : ",", message->param[index]);
    }
    (void)printf("]\n");
    (void)fflush(stdout);
}

static void *application_thread(void *argument)
{
    AppThreadArgs *args = (AppThreadArgs *)argument;
    Message_t message;
    int result;
    for (;;) {
        result = queue_pop(args->queue, &message);
        if (result != 0) {
            break;
        }
        print_message(args->name, &message);
    }
    return NULL;
}

static int decode_message(const uint8_t *bytes, size_t length, Message_t *message)
{
    uint32_t value;
    size_t index;
    if (length != MSG_WIRE_SIZE) {
        return -1;
    }
    memcpy(&value, &bytes[0], sizeof(value));
    message->app_id = ntohl(value);
    memcpy(&value, &bytes[sizeof(value)], sizeof(value));
    message->command_id = ntohl(value);
    for (index = 0U; index < MSG_PARAM_COUNT; index++) {
        memcpy(&value, &bytes[(index + 2U) * sizeof(value)], sizeof(value));
        message->param[index] = ntohl(value);
    }
    return 0;
}

static int elapsed_at_least_one_second(const struct timespec *now,
                                       const struct timespec *last)
{
    time_t seconds = now->tv_sec - last->tv_sec;
    if (seconds > 1) {
        return 1;
    }
    if (seconds < 1) {
        return 0;
    }
    return now->tv_nsec >= last->tv_nsec;
}

static void wake_queues(void)
{
    MessageQueue *queues[2] = { &app1_queue, &app2_queue };
    size_t index;
    for (index = 0U; index < 2U; index++) {
        int result = pthread_mutex_lock(&queues[index]->mutex);
        if (result == 0) {
            (void)pthread_cond_broadcast(&queues[index]->not_empty);
            (void)pthread_cond_broadcast(&queues[index]->not_full);
            (void)pthread_mutex_unlock(&queues[index]->mutex);
        }
    }
}

static void *router_thread(void *unused)
{
    int socket_fd;
    struct sockaddr_in address;
    struct timeval timeout = { .tv_sec = 0, .tv_usec = 250000 };
    uint8_t packet[MSG_WIRE_SIZE + 1U];
    struct timespec last_accepted = { 0, 0 };
    int have_last_accepted = 0;
    (void)unused;

    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        (void)fprintf(stderr, "socket: %s\n", strerror(errno));
        return NULL;
    }
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        (void)fprintf(stderr, "setsockopt(SO_RCVTIMEO): %s\n", strerror(errno));
        (void)close(socket_fd);
        return NULL;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)UDP_ROUTER_PORT);
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        (void)fprintf(stderr, "bind port %u: %s\n", UDP_ROUTER_PORT, strerror(errno));
        (void)close(socket_fd);
        return NULL;
    }
    (void)printf("UDP router listening on port %u\n", UDP_ROUTER_PORT);
    (void)fflush(stdout);

    while (stop_requested == 0) {
        ssize_t received = recvfrom(socket_fd, packet, sizeof(packet), 0, NULL, NULL);
        Message_t message;
        MessageQueue *destination;
        struct timespec now;
        int result;
        if (received < 0) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR)) {
                continue;
            }
            (void)fprintf(stderr, "recvfrom: %s\n", strerror(errno));
            continue;
        }
        if (decode_message(packet, (size_t)received, &message) != 0) {
            (void)fprintf(stderr, "discarded malformed datagram: expected %u bytes, received %" PRIuMAX "\n",
                          (unsigned int)MSG_WIRE_SIZE, (uintmax_t)received);
            continue;
        }
        if (message.app_id == APP_ID_1) {
            destination = &app1_queue;
        } else if (message.app_id == APP_ID_2) {
            destination = &app2_queue;
        } else {
            (void)fprintf(stderr, "discarded unsupported app_id=%" PRIu32 "\n", message.app_id);
            continue;
        }
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            (void)fprintf(stderr, "clock_gettime: %s\n", strerror(errno));
            continue;
        }
        if ((have_last_accepted != 0) &&
            (elapsed_at_least_one_second(&now, &last_accepted) == 0)) {
            (void)fprintf(stderr, "discarded rate-limited message\n");
            continue;
        }
        result = queue_push(destination, &message);
        if (result < 0) {
            break;
        }
        if (result == 0) {
            if (clock_gettime(CLOCK_MONOTONIC, &last_accepted) != 0) {
                (void)fprintf(stderr, "clock_gettime: %s\n", strerror(errno));
                last_accepted = now;
            }
            have_last_accepted = 1;
        }
    }
    (void)close(socket_fd);
    stop_requested = 1;
    wake_queues();
    return NULL;
}

int main(void)
{
    pthread_t router;
    pthread_t app1;
    pthread_t app2;
    AppThreadArgs app1_args = { &app1_queue, "Application 1" };
    AppThreadArgs app2_args = { &app2_queue, "Application 2" };
    struct sigaction action;
    int app1_created = 0;
    int app2_created = 0;
    int router_created = 0;
    int exit_status = 0;
    int result;

    if (queue_init(&app1_queue) != 0) {
        return 1;
    }
    if (queue_init(&app2_queue) != 0) {
        queue_destroy(&app1_queue);
        return 1;
    }
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    (void)sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) != 0) {
        (void)fprintf(stderr, "sigaction: %s\n", strerror(errno));
        exit_status = 1;
        goto cleanup;
    }

    result = pthread_create(&app1, NULL, application_thread, &app1_args);
    if (result != 0) {
        (void)fprintf(stderr, "pthread_create app1: %s\n", strerror(result));
        exit_status = 1;
        goto cleanup;
    }
    app1_created = 1;
    result = pthread_create(&app2, NULL, application_thread, &app2_args);
    if (result != 0) {
        (void)fprintf(stderr, "pthread_create app2: %s\n", strerror(result));
        exit_status = 1;
        stop_requested = 1;
        wake_queues();
        goto cleanup;
    }
    app2_created = 1;
    result = pthread_create(&router, NULL, router_thread, NULL);
    if (result != 0) {
        (void)fprintf(stderr, "pthread_create router: %s\n", strerror(result));
        exit_status = 1;
        stop_requested = 1;
        wake_queues();
        goto cleanup;
    }
    router_created = 1;
    result = pthread_join(router, NULL);
    if (result != 0) {
        (void)fprintf(stderr, "pthread_join router: %s\n", strerror(result));
        exit_status = 1;
        stop_requested = 1;
    }
    wake_queues();

cleanup:
    if (app1_created != 0) {
        result = pthread_join(app1, NULL);
        if (result != 0) {
            (void)fprintf(stderr, "pthread_join app1: %s\n", strerror(result));
            exit_status = 1;
        }
    }
    if (app2_created != 0) {
        result = pthread_join(app2, NULL);
        if (result != 0) {
            (void)fprintf(stderr, "pthread_join app2: %s\n", strerror(result));
            exit_status = 1;
        }
    }
    if (router_created == 0) {
        wake_queues();
    }
    queue_destroy(&app2_queue);
    queue_destroy(&app1_queue);
    return exit_status;
}
