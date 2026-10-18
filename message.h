#ifndef UDP_MESSAGE_ROUTER_MESSAGE_H
#define UDP_MESSAGE_ROUTER_MESSAGE_H

#include <stdint.h>

#define UDP_ROUTER_PORT 9876U
#define APP_ID_1 1U
#define APP_ID_2 2U
#define MSG_PARAM_COUNT 256U
#define MSG_WORD_COUNT (2U + MSG_PARAM_COUNT)
#define MSG_WIRE_SIZE (MSG_WORD_COUNT * sizeof(uint32_t))
#define APP_QUEUE_CAPACITY 8U

typedef struct {
    uint32_t app_id;
    uint32_t command_id;
    uint32_t param[MSG_PARAM_COUNT];
} Message_t;

#endif
