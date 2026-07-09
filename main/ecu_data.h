#ifndef ECU_DATA_H
#define ECU_DATA_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

// ECU Data structure based on VW CAN bus spec
typedef struct {
  // Engine Parameters
  float engine_rpm;
  float tps_position;
  float abs_pedal_pos;
  float map_kpa;
  float clt_temp;        // Coolant Temperature
  float iat_temp;        // Intake Air Temperature
  float oil_temp;        // Oil Temperature
  float oil_pressure;    // Oil Pressure (kPa)
  float vehicle_speed;   // Vehicle Speed (km/h)
  float battery_voltage; // Battery Voltage (V)

  // Boost Control
  float wg_set_percent;
  float wg_pos_percent;
  float bov_percent;
  float target_boost;

  // Torque Values (Nm)
  float tcu_tq_req_nm;
  float tcu_tq_act_nm;
  float eng_trg_nm;
  float eng_act_nm;
  float limit_tq_nm;
  int8_t gear;              // Gear Position
  int8_t selector_position; // Selector Lever Position

  // New Powertrain Metrics
  float trans_temp;
  float afr_val;
  float afr_target;
  float egt_temp;
  float knock_retard;
  float ambient_temp;
  float mre_map_kpa;
  float mre_wg_pos_percent;

  // Launch Control Diagnostics
  bool launch_control_active;  // Tra_stLnchCtlActv
  bool tcu_launch_ready;       // GE1_LaunchControl (0x440 byte 6 bit 0)
  uint8_t gear_lever_val;      // Gbx_stGearLvr (e.g. 12 = Sport)
  float pedal_position;        // APP_r (0-100%)
  uint8_t brake_status;        // Brk_st (e.g. 3 = pressed)
  bool tcu_torque_intervention;// Tra_stTSC.5

  // System
  bool dsg_shift_active;
  bool dsg_blip_active;
  bool asr_active;
  bool esp_active;

  // CAN1 Raw (Powertrain) Values
  float engine_rpm_raw;
  float tps_position_raw;
  float abs_pedal_pos_raw;
  float map_kpa_raw;
  float clt_temp_raw;
  float iat_temp_raw;
  float oil_temp_raw;
  float vehicle_speed_raw;
  float battery_voltage_raw;
  float afr_val_raw;
  float afr_target_raw;
  int8_t gear_raw;
  int8_t selector_position_raw;
  float ambient_temp_raw;

  // CAN2 Diagnostic (OBD2 Polled) Values
  float engine_rpm_diag;
  float tps_position_diag;
  float abs_pedal_pos_diag;
  float map_kpa_diag;
  float clt_temp_diag;
  float iat_temp_diag;
  float oil_temp_diag;
  float vehicle_speed_diag;
  float battery_voltage_diag;
  float afr_val_diag;
  float afr_target_diag;
  int8_t gear_diag;
  int8_t selector_position_diag;
  float ambient_temp_diag;

  // Telemetry update timestamps
  uint32_t last_raw_update_ms[32];
  uint32_t last_diag_update_ms[32];

  uint64_t timestamp;
} ecu_data_t;

// System settings
typedef struct {
  float max_boost_limit; // Maximum boost limit
  float max_rpm_limit;   // Maximum RPM limit

  bool audio_alerts_enabled; // Audio alerts enabled
  char ecu_address[32];      // ECU address

  // Gauge Visibility Settings (Screen 1)
  bool show_map;
  bool show_wastegate;
  bool show_tps;
  bool show_rpm;
  bool show_boost;
  bool show_tcu;

  // Screen 2
  bool show_oil_press;
  bool show_oil_temp;
  bool show_water_temp;
  bool show_fuel_press;
  bool show_battery;

  // Screen 4
  bool show_pedal;
  bool show_wg_pos;
  bool show_bov;
  bool show_tcu_req;
  bool show_tcu_act;
  bool show_eng_req;

  // Screen 5
  bool show_eng_act;
  bool show_limit_tq;

  // New Gauges
  bool show_iat;
  bool show_speed;
  bool show_trans_temp;
  bool show_afr;
  bool show_egt;
  bool show_knock_retard;
  bool show_boost_act;
  bool show_ambient_temp;
  bool show_mre_map;
  bool show_mre_wastegate;
  bool mre_parallel;
  bool send_ambient_temp_to_can;
  float ambient_can_temp;

  // Unit settings for each gauge
  uint8_t gauge_units[32];

  // Source settings for each gauge (0=AUTO, 1=CAN1_RAW, 2=CAN2_DIAG)
  uint8_t gauge_sources[32];

  // Dynamic Layout Settings
  int active_gauge_ids[24]; // Ordered list of enabled gauge IDs
  int active_gauge_count;   // Number of active gauges
  bool screen3_enabled;
  uint32_t screen_brightness; // Added for P4 compatibility

  // VAG Diagnostic Emulation Settings
  uint8_t diag_address;       // Diagnostic address (e.g. 0x3D)
  uint8_t diag_protocol;      // Protocol: 0=Disabled, 1=UDS, 2=TP2.0/KWP2000, 3=Both
  char diag_part_number[16];  // VAG Part Number string (Software Number)
  char diag_comp_name[32];    // VAG Component Name string (System Description)
  char diag_hw_number[16];    // VAG Hardware Number string
  char diag_sw_version[8];    // VAG Software Version string
  char diag_vin[20];          // VAG VIN string
  uint32_t diag_coding;       // VAG Coding (integer)
} system_settings_t;


// Gauge IDs for dynamic layout
typedef enum {
  GAUGE_NONE = -1,

  // Screen 1 Gauges
  GAUGE_MAP = 0,
  GAUGE_WASTEGATE,
  GAUGE_TPS,
  GAUGE_RPM,
  GAUGE_BOOST,
  GAUGE_TCU,

  // Screen 2 Gauges
  GAUGE_OIL_PRESS,
  GAUGE_OIL_TEMP,
  GAUGE_WATER_TEMP,
  GAUGE_FUEL_PRESS,
  GAUGE_BATTERY,

  // Screen 4 Gauges
  GAUGE_PEDAL,
  GAUGE_WG_POS,
  GAUGE_BOV,
  GAUGE_TCU_REQ,
  GAUGE_TCU_ACT,
  GAUGE_ENG_REQ,

  // Screen 5 Gauges
  GAUGE_ENG_ACT,
  GAUGE_LIMIT_TQ,

  // New Gauges
  GAUGE_IAT,
  GAUGE_SPEED,
  GAUGE_TRANS_TEMP,
  GAUGE_AFR,
  GAUGE_EGT,
  GAUGE_KNOCK_RETARD,
  GAUGE_BOOST_ACT,
  GAUGE_AMBIENT_TEMP,
  GAUGE_MRE_MAP,
  GAUGE_MRE_WASTEGATE,

  GAUGE_MAX
} gauge_id_t;

// Connection status
typedef struct {
  bool connected;
  char message[128];
} connection_status_t;

// Data stream entry
typedef enum { LOG_INFO, LOG_WARNING, LOG_SUCCESS, LOG_ERROR } log_type_t;

typedef struct {
  uint64_t timestamp;
  char message[256];
  log_type_t type;
} data_stream_entry_t;

// Function prototypes
void ecu_data_init(void);
void ecu_data_update(ecu_data_t *data);
typedef void (*ecu_data_update_fn_t)(ecu_data_t *state, void *ctx);
void ecu_data_update_transaction(ecu_data_update_fn_t update_fn, void *ctx);
ecu_data_t *ecu_data_get(void);                // Unsafe, for internal use
void ecu_data_get_copy(ecu_data_t *data_copy); // Thread-safe getter
char *ecu_data_to_json(const ecu_data_t *data);
bool ecu_data_from_json(const char *json_str, ecu_data_t *data);
void ecu_data_simulate(ecu_data_t *data);

// System settings functions
void system_settings_init(void);
system_settings_t *system_settings_get(void);
void system_settings_save(const system_settings_t *settings);

// Logging functions
void data_stream_add_entry(const char *message, log_type_t type);
void data_stream_clear(void);
char *data_stream_to_json(void);

// Simple data functions for WiFi server
char *ecu_data_to_string(const ecu_data_t *data);
char *data_stream_to_string(void);

#ifdef __cplusplus
}
#endif

#endif // ECU_DATA_H
