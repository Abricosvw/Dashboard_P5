#ifndef DIAGNOSTIC_TESTER_H
#define DIAGNOSTIC_TESTER_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/twai.h"

#ifdef __cplusplus
extern "C" {
#endif

// Tester states
typedef enum {
    TESTER_IDLE,
    TESTER_SCANNING,
    TESTER_CLEARING,
    TESTER_FINISHED
} tester_state_t;

/**
 * @brief Initialize the diagnostic tester engine
 */
void diagnostic_tester_init(void);

/**
 * @brief Starts an asynchronous diagnostic scan of all powertrain modules
 */
void diagnostic_tester_start_scan(void);

/**
 * @brief Starts an asynchronous diagnostic DTC clearing process for all powertrain modules
 */
void diagnostic_tester_start_clear(void);

/**
 * @brief Gets the current execution state of the diagnostic tester
 */
tester_state_t diagnostic_tester_get_state(void);

/**
 * @brief Gets the progress percentage of the current operation (0 to 100)
 */
int diagnostic_tester_get_progress(void);

/**
 * @brief Gets the scrollable log buffer text containing DTC details
 */
const char* diagnostic_tester_get_logs(void);

/**
 * @brief Clears the log buffer
 */
void diagnostic_tester_clear_logs(void);

/**
 * @brief Hook called by the main CAN RX task to intercept diagnostic response frames
 * @param msg The received message
 * @return true if the message was intercepted, false otherwise
 */
bool diagnostic_tester_handle_rx(const twai_message_t *msg);

#ifdef __cplusplus
}
#endif

#endif // DIAGNOSTIC_TESTER_H
