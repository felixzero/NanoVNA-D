/*
 * trace_analysis.h - trace analysis module for NanoVNA-D firmware
 *
 * Copyright (c) 2026, EDF R&D 
 * Based on DiSlord/NanoVNA-D by Dmitry (DiSlord) 
 */

#ifndef TRACE_ANALYSIS_H
#define TRACE_ANALYSIS_H

#if defined(NANOVNA_F303)
#define __USE_TRACE_ANALYSIS__
#endif

#ifdef __USE_TRACE_ANALYSIS__
#include "nanovna.h"
#include <stdint.h>
#include <stdbool.h>

#define ANALYSIS_POINTS    250 // We kept the value below 260 to avoid a memory overflow.  This restricts functionality to sweep points of 51, 101, and 201 only.

#define ANALYSIS_SHOW_MIN  (1u << 0)
#define ANALYSIS_SHOW_MAX  (1u << 1)
#define ANALYSIS_SHOW_BOTH (ANALYSIS_SHOW_MIN | ANALYSIS_SHOW_MAX)

typedef struct {
  bool     active;
  bool     initialized;
  uint8_t  trace_index;
  uint8_t  show_flags;
  int16_t  min_y[ANALYSIS_POINTS];
  int16_t  max_y[ANALYSIS_POINTS];
  uint8_t  valid[ANALYSIS_POINTS];
} trace_analysis_t;

extern trace_analysis_t g_analysis;

void     analysis_start(uint8_t trace_idx);
void     analysis_stop(void);
void     analysis_reset(void);
void     analysis_update_point(uint8_t t, uint16_t i, int16_t y_pixel);
void     analysis_draw_cell(int x0, int y0, int w, int h);
uint16_t analysis_valid_count(void);

#endif

#endif /* TRACE_ANALYSIS_H */