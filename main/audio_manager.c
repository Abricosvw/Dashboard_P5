#include "audio_manager.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "AUDIO_MGR";
extern i2c_master_bus_handle_t i2c1_bus;
static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;
static uint32_t current_sample_rate = AUDIO_SAMPLE_RATE;
static i2c_master_dev_handle_t s_codec_i2c_handle = NULL;
static int s_current_volume_percent = 50;

// Define ES8311 Address if not defined (Fallback)
#ifndef ES8311_ADDRESS_0
#define ES8311_ADDRESS_0 0x18
#endif

// ES8311 Registers
#define ES8311_REG_RESET 0x00
#define ES8311_REG_CLK_MANAGER 0x01
#define ES8311_REG_CLK_DIV1 0x02
#define ES8311_REG_CLK_DIV2 0x03
#define ES8311_REG_CLK_SRC 0x04
#define ES8311_REG_CLK_DIV3 0x05
#define ES8311_REG_AIF_DIV 0x06
#define ES8311_REG_AIF_TRI 0x07
#define ES8311_REG_AIF_CH 0x08
#define ES8311_REG_SDPIN 0x09
#define ES8311_REG_SDPOUT 0x0A
#define ES8311_REG_SYSTEM_0D 0x0D // Power up/down
#define ES8311_REG_SYSTEM_0E 0x0E // ADC Power up/down
#define ES8311_REG_SYSTEM_12 0x12 // Enable DAC
#define ES8311_REG_SYSTEM_13 0x13 // HP drive
#define ES8311_REG_SYSTEM_14 0x14 // PGA / Format
#define ES8311_REG_ADC_VOL 0x17
#define ES8311_REG_DAC_SET1 0x31 // Mute control
#define ES8311_REG_DAC_VOL 0x32  // Volume control
#define ES8311_REG_DAC_SET2 0x37 // Ramp/Setting
#define ES8311_REG_GPIO 0x44

static esp_err_t es8311_write_reg(uint8_t reg_addr, uint8_t data) {
  if (!s_codec_i2c_handle)
    return ESP_ERR_INVALID_STATE;
  uint8_t write_buf[2] = {reg_addr, data};
  return i2c_master_transmit(s_codec_i2c_handle, write_buf, sizeof(write_buf),
                             -1);
}

// Forward declaration
esp_err_t audio_set_sample_rate_internal(uint32_t rate);

static esp_err_t audio_codec_config(uint32_t sample_rate) {
  if (!s_codec_i2c_handle)
    return ESP_FAIL;

  // 1. Reset and Power-On Sequence (Official Driver)
  ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_REG_RESET, 0x1F), TAG,
                      "Reset failed");
  vTaskDelay(pdMS_TO_TICKS(10));
  ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_REG_RESET, 0x00), TAG,
                      "Reset release failed");
  ESP_RETURN_ON_ERROR(es8311_write_reg(ES8311_REG_RESET, 0x80), TAG,
                      "Power-on failed");

  // 2. Clock Management (Configure for MCLK = 256 * fs)
  ESP_RETURN_ON_ERROR(es8311_write_reg(0x01, 0x3F), TAG,
                      "Clk 01 failed"); // All clocks on
  ESP_RETURN_ON_ERROR(es8311_write_reg(0x02, 0x00), TAG,
                      "Clk 02 failed"); // Pre-div 1
  ESP_RETURN_ON_ERROR(es8311_write_reg(0x03, 0x10), TAG,
                      "Clk 03 failed"); // OSR 16 (Single Speed)
  ESP_RETURN_ON_ERROR(es8311_write_reg(0x04, 0x10), TAG,
                      "Clk 04 failed"); // DAC OSR 16
  ESP_RETURN_ON_ERROR(es8311_write_reg(0x05, 0x00), TAG,
                      "Clk 05 failed"); // ADC/DAC div 1
  ESP_RETURN_ON_ERROR(es8311_write_reg(0x06, 0x03), TAG,
                      "Clk 06 failed"); // BCLK=MCLK/4 (64fs)
  ESP_RETURN_ON_ERROR(es8311_write_reg(0x07, 0x00), TAG,
                      "Clk 07 failed"); // LRCK div High
  ESP_RETURN_ON_ERROR(es8311_write_reg(0x08, 0xFF), TAG,
                      "Clk 08 failed"); // LRCK div Low (256)

  // 3. Serial Data Format (16-bit I2S)
  ESP_RETURN_ON_ERROR(es8311_write_reg(0x09, 0x0C), TAG, "Format 09 failed");
  ESP_RETURN_ON_ERROR(es8311_write_reg(0x0A, 0x0C), TAG, "Format 0A failed");

  // 4. Power Management & Signal Path
  ESP_RETURN_ON_ERROR(es8311_write_reg(0x0D, 0x01), TAG,
                      "Sys 0D failed"); // Analog power
  ESP_RETURN_ON_ERROR(es8311_write_reg(0x0E, 0x02), TAG,
                      "Sys 0E failed"); // ADC / PGA
  ESP_RETURN_ON_ERROR(es8311_write_reg(0x12, 0x00), TAG,
                      "Sys 12 failed"); // DAC power
  ESP_RETURN_ON_ERROR(es8311_write_reg(0x13, 0x10), TAG,
                      "Sys 13 failed"); // HP Drive enable
  ESP_RETURN_ON_ERROR(es8311_write_reg(0x14, 0x1A), TAG,
                      "Sys 14 failed"); // PGA Gain / Format

  // 5. User Specific: Mono Mix & Clean High Gain
  ESP_RETURN_ON_ERROR(es8311_write_reg(0x43, 0x80), TAG,
                      "Mono Mix failed"); // Stereo to Mono mix
  ESP_RETURN_ON_ERROR(es8311_write_reg(0x38, 0x22), TAG,
                      "Driver Gain failed"); // Max driver gain (+6dB)
  ESP_RETURN_ON_ERROR(es8311_write_reg(0x17, 0xC0), TAG,
                      "ADC Vol failed"); // ADC Vol 0dB

  // 6. Volume & Unmute
  audio_set_volume(s_current_volume_percent);
  ESP_RETURN_ON_ERROR(es8311_write_reg(0x31, 0x00), TAG, "Unmute failed");

  ESP_LOGI(TAG, "ES8311 Configured for %" PRIu32 " Hz (Mono Setup)",
           sample_rate);
  return ESP_OK;
}

static esp_err_t audio_codec_dump_regs(void) {
  if (!s_codec_i2c_handle)
    return ESP_FAIL;
  ESP_LOGI(TAG, "--- ES8311 Register Dump ---");
  uint8_t regs[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
                    0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
                    0x16, 0x17, 0x31, 0x32, 0x37, 0x38, 0x43, 0x44};
  for (int i = 0; i < sizeof(regs); i++) {
    uint8_t val = 0;
    i2c_master_transmit_receive(s_codec_i2c_handle, &regs[i], 1, &val, 1, -1);
    ESP_LOGI(TAG, "Reg 0x%02X: 0x%02X", regs[i], val);
  }
  ESP_LOGI(TAG, "----------------------------");
  return ESP_OK;
}

static esp_err_t audio_codec_init(void) {
  // Initialize ES8311 Codec using New I2C Driver
  if (!i2c1_bus) {
    ESP_LOGE(TAG, "I2C Bus not initialized");
    return ESP_FAIL;
  }

  i2c_device_config_t dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = ES8311_ADDRESS_0,
      .scl_speed_hz = 100000,
  };

  if (s_codec_i2c_handle == NULL) {
    ESP_RETURN_ON_ERROR(
        i2c_master_bus_add_device(i2c1_bus, &dev_cfg, &s_codec_i2c_handle), TAG,
        "Failed to add ES8311 I2C device");
    ESP_LOGI(TAG, "ES8311 I2C Device added");
  }

  esp_err_t err = audio_codec_config(current_sample_rate);
  audio_codec_dump_regs();
  return err;
}

static esp_err_t audio_i2s_init(void) {
  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle), TAG,
                      "I2S new channel failed");

  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(current_sample_rate),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .mclk = AUDIO_MCLK_IO,
              .bclk = AUDIO_BCLK_IO,
              .ws = AUDIO_WS_IO,
              .dout = AUDIO_DOUT_IO,
              .din = AUDIO_DIN_IO,
          },
  };
  // Enable MCLK output
  std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

  ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx_handle, &std_cfg), TAG,
                      "I2S TX init failed");
  ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx_handle, &std_cfg), TAG,
                      "I2S RX init failed");

  ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_handle), TAG,
                      "I2S TX enable failed");
  ESP_RETURN_ON_ERROR(i2s_channel_enable(rx_handle), TAG,
                      "I2S RX enable failed");

  return ESP_OK;
}

// Helper to switch sample rate dynamically
esp_err_t audio_set_sample_rate_internal(uint32_t rate) {
  if (rate == current_sample_rate)
    return ESP_OK;

  ESP_LOGI(TAG, "Switching Sample Rate: %" PRIu32 " Hz -> %" PRIu32 " Hz",
           current_sample_rate, rate);

  // 1. Disable I2S
  i2s_channel_disable(tx_handle);

  // 2. Reconfigure I2S Clock
  i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate);
  clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(tx_handle, &clk_cfg), TAG,
                      "I2S reconfig failed");

  // 3. Enable I2S (Clocks must be active for codec to sync during config)
  i2s_channel_enable(tx_handle);
  vTaskDelay(pdMS_TO_TICKS(10));

  // 4. Reconfigure Codec
  audio_codec_config(rate);

  current_sample_rate = rate;
  return ESP_OK;
}

esp_err_t audio_init(void) {
  ESP_LOGI(TAG, "Initializing Audio System (Variant 1)...");

  // 1. Power Amplifier Enable (GPIO 53)
  ESP_LOGI(TAG, "Enabling Power Amplifier (GPIO %d)...", AUDIO_PA_ENABLE_IO);
  gpio_config_t pa_cfg = {
      .pin_bit_mask = (1ULL << AUDIO_PA_ENABLE_IO),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&pa_cfg));
  gpio_set_level(AUDIO_PA_ENABLE_IO, 1);
  vTaskDelay(pdMS_TO_TICKS(50)); // Wait for amp to stabilize

  // 2. Initialize I2S first to provide MCLK
  ESP_RETURN_ON_ERROR(audio_i2s_init(), TAG, "I2S init failed");

  // 3. Initialize Codec
  ESP_RETURN_ON_ERROR(audio_codec_init(), TAG, "Codec init failed");

  ESP_LOGI(TAG, "Audio System Initialized Successfully");
  return ESP_OK;
}

esp_err_t audio_play_tone(uint32_t freq_hz, uint32_t duration_ms) {
  if (!tx_handle)
    return ESP_FAIL;

  ESP_LOGI(TAG, "Playing test tone: %" PRIu32 " Hz for %" PRIu32 " ms", freq_hz,
           duration_ms);

  size_t bytes_written = 0;
  uint32_t sample_rate = AUDIO_SAMPLE_RATE;
  uint32_t samples = (sample_rate * duration_ms) / 1000;
  int16_t *buffer = malloc(samples * 2 * sizeof(int16_t)); // Stereo

  if (!buffer) {
    ESP_LOGE(TAG, "Failed to allocate audio buffer");
    return ESP_ERR_NO_MEM;
  }

  for (int i = 0; i < samples; i++) {
    double t = (double)i / sample_rate;
    int16_t val =
        (int16_t)(sin(2 * M_PI * freq_hz * t) * 15000); // Amplitude 15000/32767
    buffer[i * 2] = val;                                // Left
    buffer[i * 2 + 1] = val;                            // Right
  }

  i2s_channel_write(tx_handle, buffer, samples * 2 * sizeof(int16_t),
                    &bytes_written, 1000);

  free(buffer);
  return ESP_OK;
}

// WAV Header Structure (simplified for parsing)
typedef struct {
  char riff_header[4]; // "RIFF"
  uint32_t wav_size;
  char wave_header[4]; // "WAVE"
  char fmt_header[4];  // "fmt "
  uint32_t fmt_chunk_size;
  uint16_t audio_format;
  uint16_t num_channels;
  uint32_t sample_rate;
  uint32_t byte_rate;
  uint16_t sample_alignment;
  uint16_t bit_depth;
  char data_header[4]; // "data"
  uint32_t data_bytes;
} wav_header_t;

esp_err_t audio_play_wav(const char *path) {
  if (!tx_handle) {
    ESP_LOGE(TAG, "I2S not initialized");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Playing WAV file: %s", path);

  // Basic check: if path ends with '/', it's a directory
  if (path == NULL || strlen(path) == 0 || path[strlen(path) - 1] == '/') {
    ESP_LOGE(TAG, "Invalid WAV path: %s", path ? path : "NULL");
    return ESP_FAIL;
  }

  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    ESP_LOGE(TAG, "Failed to open WAV file: %s (errno: %d)", path, errno);
    return ESP_FAIL;
  }

  // Read Header
  wav_header_t header;
  if (fread(&header, 1, sizeof(wav_header_t), f) != sizeof(wav_header_t)) {
    ESP_LOGE(TAG, "Failed to read WAV header");
    fclose(f);
    return ESP_FAIL;
  }

  // Debug Output
  ESP_LOGI(TAG, "WAV Header Details:");
  ESP_LOGI(TAG, "  RIFF: %.4s", header.riff_header);
  ESP_LOGI(TAG, "  WAVE: %.4s", header.wave_header);
  ESP_LOGI(TAG, "  Format: %d (1=PCM)", header.audio_format);
  ESP_LOGI(TAG, "  Channels: %d", header.num_channels);
  ESP_LOGI(TAG, "  Sample Rate: %" PRIu32 " Hz", header.sample_rate);
  ESP_LOGI(TAG, "  Bit Depth: %d bits", header.bit_depth);
  ESP_LOGI(TAG, "  Data Bytes: %" PRIu32, header.data_bytes);

  // Validate basic PCM
  if (strncmp(header.riff_header, "RIFF", 4) != 0 ||
      strncmp(header.wave_header, "WAVE", 4) != 0) {
    ESP_LOGE(TAG, "Invalid WAV file format");
    fclose(f);
    return ESP_FAIL;
  }

  // Auto-switch sample rate if supported (added)
  if (header.sample_rate > 0 && header.sample_rate <= 48000) {
    audio_set_sample_rate_internal(header.sample_rate);
  } else {
    ESP_LOGW(TAG, "Unsupported WAV sample rate: %" PRIu32, header.sample_rate);
  }

  // Search for "data" chunk
  uint32_t chunk_id;
  uint32_t wav_chunk_size;
  bool data_found = false;

  long current_offset = 12; // Start after RIFF + Size + WAVE (4+4+4)
  fseek(f, current_offset, SEEK_SET);

  while (fread(&chunk_id, 1, 4, f) == 4 &&
         fread(&wav_chunk_size, 1, 4, f) == 4) {
    if (chunk_id ==
        0x61746164) { // "data" in little endian is 0x61746164 ('d','a','t','a')
      data_found = true;
      ESP_LOGI(TAG, "Found data chunk at offset %ld, size: %" PRIu32, ftell(f),
               wav_chunk_size);
      header.data_bytes = wav_chunk_size;
      break;
    }

    // Skip this chunk
    fseek(f, wav_chunk_size, SEEK_CUR);
  }

  if (!data_found) {
    ESP_LOGE(TAG, "WAV 'data' chunk not found");
    fclose(f);
    // Fallback: try standard 44 bytes if search failed, though unlikely to work
    // well
    return ESP_FAIL;
  }

  // Buffer for reading
  const size_t chunk_size = 1024;
  int16_t *buffer = malloc(chunk_size);
  if (!buffer) {
    ESP_LOGE(TAG, "Memory allocation failed");
    fclose(f);
    return ESP_ERR_NO_MEM;
  }

  size_t bytes_read = 0;
  size_t bytes_written = 0;
  size_t total_bytes_played = 0;

  // Play only the data chunk
  while (total_bytes_played < header.data_bytes) {
    size_t bytes_to_read = chunk_size;

    if (total_bytes_played + chunk_size > header.data_bytes) {
      bytes_to_read = header.data_bytes - total_bytes_played;
    }

    bytes_read = fread(buffer, 1, bytes_to_read, f);
    if (bytes_read == 0)
      break; // EOF or error

    i2s_channel_write(tx_handle, buffer, bytes_read, &bytes_written, 1000);
    total_bytes_played += bytes_read;
  }

  free(buffer);
  fclose(f);
  ESP_LOGI(TAG, "WAV playback finished");
  return ESP_OK;
}

esp_err_t audio_set_volume(int volume_percent) {
  if (volume_percent < 0)
    volume_percent = 0;
  if (volume_percent > 100)
    volume_percent = 100;

  s_current_volume_percent = volume_percent;

  if (s_codec_i2c_handle) {
    // Register 0x32 DAC Volume:
    // 0x00 = -95.5dB, 0xBF = 0dB (Unity), 0xFF = +32dB (Maximum)
    // Map 0-100% to 0x80 (-32dB) up to 0xFF (+32dB) for maximum possible
    // loudness
    uint8_t vol_reg = (uint8_t)(128 + (volume_percent * (255 - 128)) / 100);
    ESP_LOGI(TAG, "Volume set to %d%% (Codec Reg: 0x%02X)", volume_percent,
             vol_reg);
    return es8311_write_reg(ES8311_REG_DAC_VOL, vol_reg);
  }
  return ESP_FAIL;
}

esp_err_t audio_record_wav(const char *path, uint32_t duration_ms) {
  if (!rx_handle) {
    ESP_LOGE(TAG, "I2S RX not initialized");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Recording to %s for %" PRIu32 " ms", path, duration_ms);
  FILE *f = fopen(path, "wb");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open file for writing");
    return ESP_FAIL;
  }

  // WAV Header placeholder
  wav_header_t header;
  memset(&header, 0, sizeof(wav_header_t));
  fwrite(&header, 1, sizeof(wav_header_t), f); // Reserve space

  // Ensure Codec ADC is ready (Configure ADC Power/Volume)
  es8311_write_reg(ES8311_REG_SYSTEM_0E, 0x00); // Power up ADC
  es8311_write_reg(ES8311_REG_ADC_VOL, 0xC0);   // 0dB

  size_t chunk_size = 1024;
  int16_t *buffer = malloc(chunk_size);
  if (!buffer) {
    fclose(f);
    return ESP_ERR_NO_MEM;
  }

  size_t bytes_read = 0;
  size_t total_bytes = 0;
  uint32_t start_time = xTaskGetTickCount();
  uint32_t target_ticks = pdMS_TO_TICKS(duration_ms);

  while ((xTaskGetTickCount() - start_time) < target_ticks) {
    if (i2s_channel_read(rx_handle, buffer, chunk_size, &bytes_read, 100) ==
        ESP_OK) {
      fwrite(buffer, 1, bytes_read, f);
      total_bytes += bytes_read;
    } else {
      vTaskDelay(1); // Yield if no data
    }
  }

  free(buffer);

  // Finalize WAV Header
  memcpy(header.riff_header, "RIFF", 4);
  header.wav_size = 36 + total_bytes;
  memcpy(header.wave_header, "WAVE", 4);
  memcpy(header.fmt_header, "fmt ", 4);
  header.fmt_chunk_size = 16;
  header.audio_format = 1; // PCM
  header.num_channels = 2; // Stereo (I2S config is stereo)
  header.sample_rate = current_sample_rate;
  header.bit_depth = 16;
  header.byte_rate =
      current_sample_rate * 2 * 2; // Rate * Channels * BytesPerSample
  header.sample_alignment = 4;
  memcpy(header.data_header, "data", 4);
  header.data_bytes = total_bytes;

  fseek(f, 0, SEEK_SET);
  fwrite(&header, 1, sizeof(wav_header_t), f);
  fclose(f);

  ESP_LOGI(TAG, "Recording complete. Size: %u bytes", total_bytes);
  return ESP_OK;
}
