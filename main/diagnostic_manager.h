#ifndef DIAGNOSTIC_MANAGER_H
#define DIAGNOSTIC_MANAGER_H

#include "driver/twai.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the diagnostic manager (resets sessions and states)
 */
void diagnostic_manager_init(void);

/**
 * @brief Intercepts and handles incoming CAN/TWAI frames for diagnostics.
 * @param msg The received message.
 * @return true if the message was handled as a diagnostic message, false otherwise.
 */
bool diagnostic_manager_handle_rx(twai_handle_t bus_handle, const twai_message_t *msg);

#ifdef __cplusplus
}
#endif

#endif // DIAGNOSTIC_MANAGER_H
