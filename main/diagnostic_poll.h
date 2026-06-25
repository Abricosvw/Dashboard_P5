#ifndef DIAGNOSTIC_POLL_H
#define DIAGNOSTIC_POLL_H

#include "esp_err.h"
#include "driver/twai.h"
#include <stdbool.h>

/**
 * @brief Initialize and start the background diagnostic polling task.
 */
void diagnostic_poll_init(void);

/**
 * @brief Hook to handle incoming CAN2 responses and pass them to the poller.
 * 
 * @param msg Received CAN frame
 * @return true if the frame was handled/consumed by the poller
 * @return false otherwise
 */
bool diagnostic_poll_handle_rx(const twai_message_t *msg);

/**
 * @brief Read DTCs from a TP2.0/KWP2000 module.
 * 
 * @param address Module logical address (e.g. 0x01, 0x02)
 * @param dtc_buf Output buffer to store raw DTC payload
 * @param dtc_len Output length of DTC payload
 * @return esp_err_t ESP_OK on success
 */
esp_err_t diagnostic_poll_read_kwp_dtcs(uint8_t address, uint8_t *dtc_buf, uint16_t *dtc_len);

/**
 * @brief Clear DTCs from a TP2.0/KWP2000 module.
 * 
 * @param address Module logical address (e.g. 0x01, 0x02)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t diagnostic_poll_clear_kwp_dtcs(uint8_t address);

#endif // DIAGNOSTIC_POLL_H
