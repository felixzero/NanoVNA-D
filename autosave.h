/*
 * autosave.h - Auto Save module for NanoVNA-D firmware
 *
 * Copyright (c) 2026, EDF R&D 
 * Based on DiSlord/NanoVNA-D by Dmitry (DiSlord) 
 */

#pragma once

#include "nanovna.h"

#ifdef __USE_SD_CARD__

//===========================================================================
// COMPILE-TIME OPTIONS
//===========================================================================

#define __USE_AUTO_SAVE__

// Buffer size for the generated filename (without extension)
#define AUTO_SAVE_FNAME_LEN   32

//===========================================================================
// PERIOD: min/max/default values
//===========================================================================
/* All internal period values ​​are in SECONDS (uint32_t).
* The user can enter in seconds (s), minutes (m), or hours (h)
* via the dedicated KEYPAD_PERIOD keyboard in ui.c.
*/

#define AUTO_SAVE_PERIOD_MIN_S    1U       // 1 second minimum
#define AUTO_SAVE_PERIOD_MAX_S    86400U   // 24 hours maximum
#define AUTO_SAVE_PERIOD_DEFAULT  10U      // 10 seconds by default

//===========================================================================
// FORMAT FLAGS (bitmask, stored in config)
//===========================================================================
#define AUTO_SAVE_FMT_S1P      (1 << 0)   // Touchstone 1-port  (.s1p)
#define AUTO_SAVE_FMT_S2P      (1 << 1)   // Touchstone 2-port  (.s2p)
#define AUTO_SAVE_FMT_CSV      (1 << 2)   // Tabulaire          (.csv)
#define AUTO_SAVE_FMT_BMP      (1 << 3)   // Screenshot         (.bmp / .tif)
#define AUTO_SAVE_FMT_DEFAULT  AUTO_SAVE_FMT_S1P

//===========================================================================
// AUTO SAVE STATE (state machine)
//===========================================================================
typedef enum {
  AS_STATE_IDLE    = 0,  // Inactive (disabled or not configured)
  AS_STATE_RUNNING = 1,  // Currently running: countdown active
  AS_STATE_SAVING  = 2,  // Writing to the SD card
} autosave_state_t;

//===========================================================================
// CONFIGURATION STRUCTURE (persistent in Flash config)
//===========================================================================
typedef struct {
  uint32_t period_s;    // Backup period in seconds
  uint8_t  format_mask; // Bitmask of active formats (AUTO_SAVE_FMT_*)
  uint8_t  enabled;     // 1 = enabled, 0 = disabled
  uint16_t reserved;    // 32-bit padding alignment : do not use
} autosave_config_t;

//===========================================================================
// RUNTIME STATE (RAM only, not persisted)
//===========================================================================
typedef struct {
  autosave_state_t state;      // Current state of the state machine
  uint32_t         counter_s;  // Time accumulated since the last save(s)
  uint32_t         save_count; // Total number of saves since boot
} autosave_runtime_t;

// Global instance (defined in autosave.c)
extern autosave_runtime_t autosave_rt;

//===========================================================================
// PUBLIC API
//===========================================================================

void autosave_init(void);
void ui_autosave_screenshot(const char *path);

void autosave_tick(uint32_t elapsed_ms);

// Control: start / stop / switch
// Each one calls config_save() to persist the enabled state.
void autosave_start(void);
void autosave_stop(void);
void autosave_toggle(void);

// Returns to the current state (AS_STATE_IDLE / RUNNING / SAVING)
autosave_state_t autosave_get_state(void);

void autosave_save_now(void);

bool autosave_do_save(void);

// Constructs the base filename (without extension) in buf.
// Format with RTC : "/AS_20241105_143022"
// Format without RTC : "/AS_0042"
void autosave_build_filename(char *buf, size_t maxlen);

// Writing individual formats to the SD card.
// Returns true if f_open + f_printf + f_close were successful.
bool autosave_write_s1p(const char *basename);
bool autosave_write_s2p(const char *basename);
bool autosave_write_csv(const char *basename);
bool autosave_write_screenshot(const char *basename);

// Converts a duration in seconds into a readable string for menu display.
// buf must be at least 12 characters long.

void autosave_format_period(char *buf, uint32_t period_s);

#endif /* __USE_SD_CARD__ */
