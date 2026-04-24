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



/*
* Helper for writing formatted code to a FatFS file.
* Replace f_printf, which requires FF_USE_STRFUNC=1.
* Use plot_printf (standard C, always available) + f_write.
*/

#define as_fprintf(fp, ...)  do { \
  char _buf[128];                 \
  UINT _bw;                       \
  plot_printf(_buf, sizeof(_buf), __VA_ARGS__); \
  f_write((fp), _buf, strlen(_buf), &_bw);      \
} while(0)

//===========================================================================
// Shortcut to access persistent configuration
//===========================================================================
#define AS_CFG (config.autosave)

//===========================================================================
//  RUNTIME STATUS
//=========================================================================
autosave_runtime_t autosave_rt = {
  .state      = AS_STATE_IDLE,
  .counter_s  = 0,
  .save_count = 0,
};

//===========================================================================
//INITIALISATION
//===========================================================================
// Called in load_settings() of main.c, after update_frequencies().

void autosave_init(void)
{
  // Default values ​​if the Flash configuration is corrupted or never written
  if (AS_CFG.period_s == 0 || AS_CFG.period_s > AUTO_SAVE_PERIOD_MAX_S)
    AS_CFG.period_s = AUTO_SAVE_PERIOD_DEFAULT;

  if (AS_CFG.format_mask == 0)
    AS_CFG.format_mask = AUTO_SAVE_FMT_DEFAULT;

  // Reset the runtime
  autosave_rt.counter_s  = 0;
  autosave_rt.save_count = 0;

  // Take the RUNNING state if enabled=1 was persisted before the reboot
  autosave_rt.state = AS_CFG.enabled ? AS_STATE_RUNNING : AS_STATE_IDLE;
}

//===========================================================================
// MAIN LOOP : autosave_tick()
//===========================================================================

void autosave_tick(uint32_t elapsed_ms)
{
  if (autosave_rt.state != AS_STATE_RUNNING)
    return;

  // Accumulation in milliseconds to avoid truncation drift
  static uint32_t accum_ms = 0;
  accum_ms += elapsed_ms;

  // Conversion in whole seconds only
  uint32_t elapsed_s = accum_ms / 1000U;
  if (elapsed_s == 0)
    return;
  accum_ms %= 1000U;

  autosave_rt.counter_s += elapsed_s;

  // Period reached → backup
  if (autosave_rt.counter_s >= AS_CFG.period_s) {
    autosave_rt.counter_s = 0;
    autosave_rt.state     = AS_STATE_SAVING;
    autosave_do_save();
    autosave_rt.state     = AS_STATE_RUNNING;
  }
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

//===========================================================================
// FILE NAME CONSTRUCTION
//===========================================================================
// With __USE_RTC__ : "/AS_YYYYMMDD_HHMMSS"  ex: /AS_20241105_143022
// Without __USE_RTC__ : "/AS_NNNN"             ex: /AS_0042

void autosave_build_filename(char *buf, size_t maxlen)
{
#ifdef __USE_RTC__
  // Same reading as cmd_time in main.c
  uint32_t tr = rtc_get_tr_bcd(); // TR en premier (sync HW)
  uint32_t dr = rtc_get_dr_bcd(); // DR en second

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

//S1P Touchstone 1-port
// Data S11 : measured[0][i][0] (real) + j * measured[0][i][1] (imag)
 
bool autosave_write_s1p(const char *basename)
{
  FIL     fp;
  FRESULT res;
  char    path[AUTO_SAVE_FNAME_LEN + 8];

  plot_printf(path, sizeof(path), "%s.s1p", basename);
  res = f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE);
  if (res != FR_OK) return false;

  as_fprintf(&fp, "! NanoVNA-H4 AutoSave\r\n");
  as_fprintf(&fp, "# Hz S RI R 50\r\n");

  for (uint16_t i = 0; i < sweep_points; i++) {
    as_fprintf(&fp, "%u %.10f %.10f\r\n",
             (unsigned)getFrequency(i),
             measured[0][i][0],
             measured[0][i][1]);
  }

  f_close(&fp);
  return true;
}


// S2P Touchstone 2-port
// S11 = measured[0], S21 = measured[1]
// S12 ≈ S21, S22 ≈ S11 (symmetric passive device approximation)


bool autosave_write_s2p(const char *basename)
{
  FIL     fp;
  FRESULT res;
  char    path[AUTO_SAVE_FNAME_LEN + 8];

  plot_printf(path, sizeof(path), "%s.s2p", basename);
  res = f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE);
  if (res != FR_OK) return false;

  as_fprintf(&fp, "! NanoVNA-H4 AutoSave\r\n");
  as_fprintf(&fp, "# Hz S RI R 50\r\n");

  for (uint16_t i = 0; i < sweep_points; i++) {
    float s11re = measured[0][i][0], s11im = measured[0][i][1];
    float s21re = measured[1][i][0], s21im = measured[1][i][1];
    // freq  S11re S11im  S21re S21im  S12re S12im  S22re S22im
    as_fprintf(&fp, "%u %.10f %.10f  %.10f %.10f  %.10f %.10f  %.10f %.10f\r\n",
             (unsigned)getFrequency(i),
             s11re, s11im,
             s21re, s21im,
             s21re, s21im,  // S12 ≈ S21
             s11re, s11im); // S22 ≈ S11
  }

  f_close(&fp);
  return true;
}


// CSV tabular format (Excel / Python / MATLAB)
// Columns : freq_hz, s11_re, s11_im, s11_db, s11_deg,
//                     s21_re, s21_im, s21_db, s21_deg
 
bool autosave_write_csv(const char *basename)
{
  FIL     fp;
  FRESULT res;
  char    path[AUTO_SAVE_FNAME_LEN + 8];

  plot_printf(path, sizeof(path), "%s.csv", basename);
  res = f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE);
  if (res != FR_OK) return false;

  as_fprintf(&fp,
    "freq_hz,s11_re,s11_im,s11_db,s11_deg,"
    "s21_re,s21_im,s21_db,s21_deg\r\n");

  for (uint16_t i = 0; i < sweep_points; i++) {
    float s11re = measured[0][i][0], s11im = measured[0][i][1];
    float s21re = measured[1][i][0], s21im = measured[1][i][1];

    // Modulus in dB and phase in degrees (fonctions vna_math.h)
    float s11mag = vna_sqrtf(s11re*s11re + s11im*s11im);
    float s21mag = vna_sqrtf(s21re*s21re + s21im*s21im);
    float s11db = (s11mag > 1e-10f) ? vna_log10f_x_10(s11mag) * 2.0f : -200.0f;
    float s21db = (s21mag > 1e-10f) ? vna_log10f_x_10(s21mag) * 2.0f : -200.0f;
    float s11deg = vna_atan2f(s11im, s11re) * (180.0f / VNA_PI);
    float s21deg = vna_atan2f(s21im, s21re) * (180.0f / VNA_PI);

    as_fprintf(&fp, "%u,%.8f,%.8f,%.4f,%.4f,%.8f,%.8f,%.4f,%.4f\r\n",
             (unsigned)getFrequency(i),
             s11re, s11im, s11db, s11deg,
             s21re, s21im, s21db, s21deg);
  }

  f_close(&fp);
  return true;
}


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

// Delegates to ui.c, which handles the cycle properly
// ui_mode_normal() → draw_all() → actual save
  ui_autosave_screenshot(path);
  return true;
}

//===========================================================================
//   MAIN BACKUP
//===========================================================================
bool autosave_do_save(void)
{
  char basename[AUTO_SAVE_FNAME_LEN];
  bool ok = false;

  autosave_build_filename(basename, sizeof(basename));

  if (AS_CFG.format_mask & AUTO_SAVE_FMT_S1P) ok |= autosave_write_s1p(basename);
  if (AS_CFG.format_mask & AUTO_SAVE_FMT_S2P) ok |= autosave_write_s2p(basename);
  if (AS_CFG.format_mask & AUTO_SAVE_FMT_CSV) ok |= autosave_write_csv(basename);
  if (AS_CFG.format_mask & AUTO_SAVE_FMT_BMP) ok |= autosave_write_screenshot(basename);

  if (ok)
    autosave_rt.save_count++;

  return ok;
}

void autosave_save_now(void)
{
  autosave_rt.state = AS_STATE_SAVING;
  autosave_do_save();
  autosave_rt.state = AS_CFG.enabled ? AS_STATE_RUNNING : AS_STATE_IDLE;
}

//===========================================================================
// FORMATTING THE PERIOD FOR MENU DISPLAY
//===========================================================================

void autosave_format_period(char *buf, uint32_t period_s)
{
  if (period_s == 0) {
    plot_printf(buf, 12, "---");
    return;
  }
  uint32_t h = period_s / 3600U;
  uint32_t m = (period_s % 3600U) / 60U;
  uint32_t s = period_s % 60U;

  if      (h > 0 && m == 0 && s == 0) plot_printf(buf, 12, "%luh",           (unsigned long)h);
  else if (h > 0 && s == 0)           plot_printf(buf, 12, "%luh%02lum",     (unsigned long)h, (unsigned long)m);
  else if (h == 0 && m > 0 && s == 0) plot_printf(buf, 12, "%lum",           (unsigned long)m);
  else if (h == 0 && m > 0)           plot_printf(buf, 12, "%lum%02lus",     (unsigned long)m, (unsigned long)s);
  else                                 plot_printf(buf, 12, "%lus",           (unsigned long)period_s);
}

#endif /* __USE_AUTO_SAVE__ */
#endif /* __USE_SD_CARD__   */