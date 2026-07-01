#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t last_counter;
    uint64_t total_lost;
    uint32_t last_delta;
    bool has_last_counter;
} packet_loss_tracker_t;

typedef struct {
    uint64_t total;
    uint32_t delta;
} packet_loss_result_t;

void packet_loss_init(packet_loss_tracker_t *tracker);
packet_loss_result_t packet_loss_update(packet_loss_tracker_t *tracker, uint32_t counter);
