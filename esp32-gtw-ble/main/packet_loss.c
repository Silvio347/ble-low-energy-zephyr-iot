#include "packet_loss.h"

#include <stdbool.h>
#include <string.h>

void packet_loss_init(packet_loss_tracker_t *tracker)
{
    memset(tracker, 0, sizeof(*tracker));
}

packet_loss_result_t packet_loss_update(packet_loss_tracker_t *tracker, uint32_t counter)
{
    packet_loss_result_t result = {
        .total = tracker->total_lost,
        .delta = 0,
    };

    if (!tracker->has_last_counter) {
        tracker->last_counter = counter;
        tracker->has_last_counter = true;
        return result;
    }

    if (counter > tracker->last_counter + 1) {
        result.delta = counter - tracker->last_counter - 1;
        tracker->total_lost += result.delta;
    }

    tracker->last_counter = counter;
    tracker->last_delta = result.delta;
    result.total = tracker->total_lost;
    return result;
}
