#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ENERGY_STATE_IDLE = 0,
    ENERGY_STATE_ENGAGED,
} energy_state_t;

void energy_manager_init(void);

/* Called from wifi.c when a WebSocket message is received */
void energy_manager_notify_activity(void);

/* Called on external events (sound trigger, camera motion) – adds energy */
void energy_manager_notify_event(void);

uint8_t           energy_manager_get_value(void);
int               energy_manager_get_zone(void);
energy_state_t    energy_manager_get_state(void);

#ifdef __cplusplus
}
#endif
