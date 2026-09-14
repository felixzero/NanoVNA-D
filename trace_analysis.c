/*
 * trace_analysis.c - trace analysis module for NanoVNA-D firmware
 *
 * Copyright (c) 2026, EDF R&D 
 * Based on DiSlord/NanoVNA-D by Dmitry (DiSlord) 
 */

#include "nanovna.h"
#include "trace_analysis.h"

#ifdef __USE_TRACE_ANALYSIS__

// 1 = synthetic pattern (display test) | 0 = actual sweep data

#define ANALYSIS_TEST_MODE 0

trace_analysis_t g_analysis = {0};

#define Y_MIN_BOUND  (OFFSETY)
#define Y_MAX_BOUND  (OFFSETY + HEIGHT)

static inline bool _y_valid(int16_t y) {
  return (y >= Y_MIN_BOUND && y <= Y_MAX_BOUND);
}

void analysis_start(uint8_t trace_idx) {
  g_analysis.trace_index = trace_idx;
  g_analysis.active      = true;
  if (g_analysis.show_flags == 0)
    g_analysis.show_flags = ANALYSIS_SHOW_BOTH;

  // Clean reset, waiting for actual sweep data
  for (uint16_t i = 0; i < ANALYSIS_POINTS; i++) {
    g_analysis.min_y[i] = (int16_t)Y_MAX_BOUND;
    g_analysis.max_y[i] = (int16_t)Y_MIN_BOUND;
    g_analysis.valid[i] = 0;
  }
  g_analysis.initialized = false;
  request_to_redraw(REDRAW_AREA);    // forces an immediate redraw
}

void analysis_stop(void)
{
    g_analysis.active = false;
    g_analysis.initialized = false;

    request_to_redraw(
        REDRAW_AREA |
        REDRAW_CELLS |
        REDRAW_MARKER |
        REDRAW_REFERENCE |
        REDRAW_GRID_VALUE |
        REDRAW_PLOT);
}

void analysis_reset(void) {
  for (uint16_t x = 0; x < ANALYSIS_POINTS; x++) {
    g_analysis.min_y[x] = (int16_t)Y_MAX_BOUND;
    g_analysis.max_y[x] = (int16_t)Y_MIN_BOUND;
    g_analysis.valid[x] = 0;
  }
  g_analysis.initialized = false;
  request_to_redraw(REDRAW_AREA);    // forces an immediate redraw
}

void analysis_update_point(uint8_t t, uint16_t i, int16_t y_pixel) {
  if (!g_analysis.active)          return;
  if (t != g_analysis.trace_index) return;
  if (sweep_points < 2)            return;

  // Clamp in the screen area
  if (y_pixel < (int16_t)Y_MIN_BOUND) y_pixel = (int16_t)Y_MIN_BOUND;
  if (y_pixel > (int16_t)Y_MAX_BOUND) y_pixel = (int16_t)Y_MAX_BOUND;

  // Mapping 1:1, sweep point i -> array index i
  if (i >= ANALYSIS_POINTS) return;

  g_analysis.valid[i] = 1;
  if (y_pixel < g_analysis.min_y[i]) g_analysis.min_y[i] = y_pixel;
  if (y_pixel > g_analysis.max_y[i]) g_analysis.max_y[i] = y_pixel;

  // Mark initialized after the last point of the sweep
  if (i == (uint16_t)(sweep_points - 1))
    g_analysis.initialized = true;
}

uint16_t analysis_valid_count(void) {
  uint16_t count = 0;
  for (uint16_t x = 0; x < ANALYSIS_POINTS; x++)
    if (g_analysis.valid[x]) count++;
  return count;
}

#endif
