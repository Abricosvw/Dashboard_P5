/**
 * @file dsp_shim.c
 * @brief Linker shim for esp-sr <-> esp-dsp symbol mismatch on ESP32-P4 (RISC-V).
 *
 * esp-sr is pre-compiled and calls dsps_bit_rev4r_fc32 as an extern symbol.
 * esp-dsp 1.8.1 with CONFIG_DSP_OPTIMIZED maps that name to the ae32 (Xtensa)
 * variant via a macro, so no plain C symbol with that name is ever emitted.
 * On RISC-V targets the ae32 assembly is a no-op stub, causing an undefined
 * reference at link time.
 *
 * This file provides a real C symbol that forwards to the ANSI implementation
 * that already exists inside dsps_fft4r_fc32_ansi.c (line 299).  We must
 * #undef the macro before defining our own function.
 */

#include "esp_err.h"
#include <stdint.h>

/* Pull in the header so the macro is active, then kill it. */
#include "dsps_fft4r.h"
#undef dsps_bit_rev4r_fc32

/* Forward-declare the real ANSI implementation (it lives inside
   dsps_fft4r_fc32_ansi.c but the macro normally hides its true name). */
extern esp_err_t dsps_bit_rev4r_direct_fc32_ansi(float *data, int N);

/* Provide the exact symbol that esp-sr's pre-compiled blob expects.
 * __attribute__((used)) prevents --gc-sections from discarding this symbol.
 * We also place it in a named section so we can KEEP it via linker flags. */
__attribute__((used, visibility("default")))
esp_err_t dsps_bit_rev4r_fc32(float *data, int N)
{
    return dsps_bit_rev4r_direct_fc32_ansi(data, N);
}
