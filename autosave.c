/*
 * autosave.c - Auto Save module for NanoVNA-D firmware
 *
 * Copyright (c) 2026, EDF R&D 
 * Based on DiSlord/NanoVNA-D by Dmitry (DiSlord) 
 */

#include "ch.h"
#include "hal.h"
#include "nanovna.h"
#include "autosave.h"
#include <string.h>
#include "FatFs/ff.h"       /* FatFS : f_open, f_printf, f_close */
#include "si5351.h"

#ifdef __USE_SD_CARD__
#ifdef __USE_AUTO_SAVE__

//===========================================================================
// Shortcut to access persistent configuration
//===========================================================================

#define AS_CFG (config.autosave)

#define AS_FPRINTF(fp, ...)  do { \
  char _buf[160];                 \
  UINT _bw;                       \
  plot_printf(_buf, sizeof(_buf), __VA_ARGS__); \
  f_write((fp), _buf, strlen(_buf), &_bw);      \
} while(0)

//===========================================================================
//  RUNTIME STATE
//=========================================================================
autosave_runtime_t autosave_rt = {
  .state      = AS_STATE_IDLE,
  .counter_s  = 0,
  .save_count = 0,
};

// Flag: autosave_tick() removes it, autosave_process_if_needed() consumes it
static volatile bool autosave_pending = false;

//===========================================================================
// WRITING FILES
//===========================================================================

static bool _write_s1p(const char *basename)
{
  FIL  fp;
  char path[AUTO_SAVE_FNAME_LEN + 8];
  plot_printf(path, sizeof(path), "%s.s1p", basename);
  if (f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
  AS_FPRINTF(&fp, "! NanoVNA-H4 AutoSave\r\n");
  AS_FPRINTF(&fp, "# Hz S RI R 50\r\n");
  for (uint16_t i = 0; i < sweep_points; i++) {
    AS_FPRINTF(&fp, "%u %.10f %.10f\r\n",
               (unsigned)getFrequency(i),
               measured[0][i][0], measured[0][i][1]);
  }
  f_close(&fp);
  return true;
}

static bool _write_s2p(const char *basename)
{
  FIL  fp;
  char path[AUTO_SAVE_FNAME_LEN + 8];
  plot_printf(path, sizeof(path), "%s.s2p", basename);
  if (f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
  AS_FPRINTF(&fp, "! NanoVNA-H4 AutoSave\r\n");
  AS_FPRINTF(&fp, "# Hz S RI R 50\r\n");
  for (uint16_t i = 0; i < sweep_points; i++) {
    float s11re = measured[0][i][0], s11im = measured[0][i][1];
    float s21re = measured[1][i][0], s21im = measured[1][i][1];
    AS_FPRINTF(&fp, "%u %.8f %.8f %.8f %.8f %.8f %.8f %.8f %.8f\r\n",
               (unsigned)getFrequency(i),
               s11re, s11im, s21re, s21im,
               s21re, s21im, s11re, s11im);
  }
  f_close(&fp);
  return true;
}

static bool _write_csv(const char *basename)
{
  FIL  fp;
  char path[AUTO_SAVE_FNAME_LEN + 8];
  plot_printf(path, sizeof(path), "%s.csv", basename);
  if (f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
  AS_FPRINTF(&fp, "freq_hz,s11_re,s11_im,s11_db,s11_deg,"
                  "s21_re,s21_im,s21_db,s21_deg\r\n");
  for (uint16_t i = 0; i < sweep_points; i++) {
    float s11re = measured[0][i][0], s11im = measured[0][i][1];
    float s21re = measured[1][i][0], s21im = measured[1][i][1];
    float s11mag = vna_sqrtf(s11re*s11re + s11im*s11im);
    float s21mag = vna_sqrtf(s21re*s21re + s21im*s21im);
    float s11db  = (s11mag > 1e-10f) ? vna_log10f_x_10(s11mag) * 2.0f : -200.0f;
    float s21db  = (s21mag > 1e-10f) ? vna_log10f_x_10(s21mag) * 2.0f : -200.0f;
    float s11deg = vna_atan2f(s11im, s11re) * (180.0f / VNA_PI);
    float s21deg = vna_atan2f(s21im, s21re) * (180.0f / VNA_PI);
    AS_FPRINTF(&fp, "%u,%.6f,%.6f,%.3f,%.3f,%.6f,%.6f,%.3f,%.3f\r\n",
               (unsigned)getFrequency(i),
               s11re, s11im, s11db, s11deg,
               s21re, s21im, s21db, s21deg);
  }
  f_close(&fp);
  return true;
}

//===========================================================================
// PUBLIC API
//===========================================================================

void autosave_init(void)
{
  if (AS_CFG.period_s < AUTO_SAVE_PERIOD_MIN_S ||
      AS_CFG.period_s > AUTO_SAVE_PERIOD_MAX_S)
    AS_CFG.period_s = AUTO_SAVE_PERIOD_DEFAULT;
  if (AS_CFG.format_mask == 0)
    AS_CFG.format_mask = AUTO_SAVE_FMT_DEFAULT;
  autosave_rt.counter_s  = 0;
  autosave_rt.save_count = 0;
  autosave_rt.state      = AS_STATE_IDLE;
  AS_CFG.enabled         = 0;
  autosave_pending       = false;
}


void autosave_tick(uint32_t elapsed_ms)
{
  if (autosave_rt.state != AS_STATE_RUNNING) return;
  if (AS_CFG.period_s < AUTO_SAVE_PERIOD_MIN_S)
    AS_CFG.period_s = AUTO_SAVE_PERIOD_MIN_S;

  static uint32_t accum_ms = 0;
  accum_ms += elapsed_ms;
  uint32_t elapsed_s = accum_ms / 1000U;
  if (elapsed_s == 0) return;
  accum_ms %= 1000U;

  autosave_rt.counter_s += elapsed_s;
  if (autosave_rt.counter_s >= AS_CFG.period_s) {
    autosave_rt.counter_s = 0;
    autosave_pending = true;  // Signal Thread1 to perform the backup
  }
}


void autosave_process_if_needed(void)
{
  if (!autosave_pending) return;
  if (autosave_rt.state != AS_STATE_RUNNING) {
    autosave_pending = false;
    return;
  }

  autosave_pending = false;
  autosave_rt.state = AS_STATE_SAVING;

  // Mount the SD card
  if (f_mount(fs_volume, "", 1) != FR_OK) {
    autosave_rt.state = AS_STATE_RUNNING;
    return;
  }

  char basename[AUTO_SAVE_FNAME_LEN];
  autosave_build_filename(basename, sizeof(basename));

  bool ok = false;
  if (AS_CFG.format_mask & AUTO_SAVE_FMT_S1P) ok |= _write_s1p(basename);
  if (AS_CFG.format_mask & AUTO_SAVE_FMT_S2P) ok |= _write_s2p(basename);
  if (AS_CFG.format_mask & AUTO_SAVE_FMT_CSV) ok |= _write_csv(basename);
  if (AS_CFG.format_mask & AUTO_SAVE_FMT_BMP) ok |= autosave_write_screenshot(basename);

  f_mount(NULL, "", 0);

  if (ok) {
    autosave_rt.save_count++;
    // Request a redraw if the screenshot has changed the display
    if (AS_CFG.format_mask & AUTO_SAVE_FMT_BMP)
      request_to_redraw(REDRAW_AREA);
  }

  autosave_rt.state = AS_STATE_RUNNING;
}

//===========================================================================
// START / STOP / TOGGLE CONTROL
//===========================================================================

void autosave_start(void)
{
  AS_CFG.enabled        = 1;
  autosave_rt.state     = AS_STATE_RUNNING;
  autosave_rt.counter_s = 0;
  config_save();
}

void autosave_stop(void)
{
  AS_CFG.enabled        = 0;
  autosave_rt.state     = AS_STATE_IDLE;
  autosave_rt.counter_s = 0;
  config_save();
}

void autosave_toggle(void)
{
  if (autosave_rt.state == AS_STATE_RUNNING)
    autosave_stop();
  else
    autosave_start();
}

autosave_state_t autosave_get_state(void)
{
  return autosave_rt.state;
}

bool autosave_do_save(void) { return true; }

//===========================================================================
// FILE NAME CONSTRUCTION
//===========================================================================
// With __USE_RTC__ : "/AS_YYYYMMDD_HHMMSS"  ex: /AS_20241105_143022
// Without __USE_RTC__ : "/AS_NNNN"             ex: /AS_0042

void autosave_build_filename(char *buf, size_t maxlen)
{
#ifdef __USE_RTC__
  // Same reading as cmd_time in main.c
  uint32_t tr = rtc_get_tr_bcd(); // TR first (HW sync)
  uint32_t dr = rtc_get_dr_bcd(); // DR second

  // BCD decoding: TR register = 0x00HHMMSS, DR = 0x00YYMMDD
  uint8_t hour  = ((tr >> 20) & 0x3) * 10 + ((tr >> 16) & 0xF);
  uint8_t min   = ((tr >> 12) & 0x7) * 10 + ((tr >>  8) & 0xF);
  uint8_t sec   = ((tr >>  4) & 0x7) * 10 + ((tr >>  0) & 0xF);
  uint8_t year  = ((dr >> 20) & 0xF) * 10 + ((dr >> 16) & 0xF);
  uint8_t month = ((dr >> 12) & 0x1) * 10 + ((dr >>  8) & 0xF);
  uint8_t day   = ((dr >>  4) & 0x3) * 10 + ((dr >>  0) & 0xF);

  plot_printf(buf, maxlen, "/AS_%02u%02u%02u_%02u%02u%02u",
             year, month, day, hour, min, sec);
#else
  // Sequential fallback if no RTC
  plot_printf(buf, maxlen, "/AS_%04u", (unsigned)autosave_rt.save_count);
#endif
}

//===========================================================================
// WRITING THE FILES
//===========================================================================

bool autosave_write_screenshot(const char *basename)
{
  char path[AUTO_SAVE_FNAME_LEN + 8];

#ifdef __SD_CARD_DUMP_TIFF__
// fixScreenshotFormat() is in ui.c but its result
// is predictable: if TIFF is enabled → .tif, otherwise → .bmp
  if (VNA_MODE(VNA_MODE_TIFF))
    plot_printf(path, sizeof(path), "%s.tif", basename);
  else
#endif
    plot_printf(path, sizeof(path), "%s.bmp", basename);
    ui_autosave_screenshot(path);
  return true;
}

//===========================================================================
// FORMATTING THE PERIOD FOR MENU DISPLAY
//===========================================================================

void autosave_format_period(char *buf, uint32_t period_s)
{
  if (period_s < AUTO_SAVE_PERIOD_MIN_S) { plot_printf(buf, 12, "---"); return; }
  unsigned h = (unsigned)(period_s / 3600U);
  unsigned m = (unsigned)((period_s % 3600U) / 60U);
  unsigned s = (unsigned)(period_s % 60U);
  if      (h > 0 && m == 0 && s == 0) plot_printf(buf, 12, "%uh",      h);
  else if (h > 0 && s == 0)           plot_printf(buf, 12, "%uh%02um", h, m);
  else if (h == 0 && m > 0 && s == 0) plot_printf(buf, 12, "%um",      m);
  else if (h == 0 && m > 0)           plot_printf(buf, 12, "%um%02us", m, s);
  else                                plot_printf(buf, 12, "%us",      (unsigned)period_s);
}

bool autosave_write_s1p(const char *b) { return _write_s1p(b); }
bool autosave_write_s2p(const char *b) { return _write_s2p(b); }
bool autosave_write_csv(const char *b) { return _write_csv(b); }

#endif /* __USE_AUTO_SAVE__ */
#endif /* __USE_SD_CARD__   */