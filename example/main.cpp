/*
 * Copyright  2017-2019 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * 
 * TFTLIB-based Window Manager Demo for i.MX RT1052
 */

#include <stdio.h>
#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_debug_console.h"
#include "fsl_elcdif.h"
#include "fsl_common.h"
#include "fsl_gpio.h"
#include "app.h"

/* CRTOS for task scheduling and DPC interrupt handling */
#include "CRTOS.hpp"

/* CRTOS HeapAllocator for memory management */
#include "HeapAllocator.hpp"

/* CRTOS HeapTracker for memory leak detection */
#include "HeapTracker.hpp"

/* CRTOS DPC Worker for interrupt bottom-half processing */
#include "DPCWorker.hpp"

/* CRTOS Futex for fast userspace synchronization */
#include "Futex.hpp"

/* CRTOS HAL Display driver for hardware-abstracted LCD control */
#include "HAL/Display.hpp"

/* CRTOS HAL SD Card and FileSystem for driver module loading */
#include "HAL/SDCard.hpp"
#include "HAL/FileSystem.hpp"

/* CRTOS Dynamic Driver Module Loader */
#include "DriverLoader.hpp"
#include "BootManager.hpp"

/* CRTOS Dynamic Application Loader */
#include "AppLoader.hpp"

/* CRTOS Driver Manager for accessing loaded drivers */
#include "Drivers/DriverManager.hpp"

/* CRTOS Display Driver IOCTL definitions */
#include "Drivers/DisplayIoctl.hpp"

/* CRTOS PinMux Driver IOCTL definitions */
#include "Drivers/PinMuxIoctl.hpp"

/* CMSIS cache functions for D-Cache flush */
#include "core_cm7.h"
#include "fsl_cache.h"

/* Linker symbols for memory regions - use END symbols to avoid overlapping with linker data/bss */
extern uint32_t __start_data_SRAM_ITC[];
extern uint32_t __end_bss_SRAM_ITC[];       // End of linker-placed data in SRAM_ITC
extern uint32_t __end_bss_SRAM_OC[];        // End of linker-placed data in SRAM_OC (OCRAM)
extern uint32_t __end_bss_BOARD_SDRAM[];     // End of linker-placed data in SDRAM
extern uint32_t __end_bss_NCACHE_REGION[];   // End of linker-placed data in NCACHE_REGION
extern uint32_t __top_SRAM_ITC[];            // Top of SRAM_ITC region
extern uint32_t __top_SRAM_OC[];             // Top of SRAM_OC region (0x20240000)
extern uint32_t __top_BOARD_SDRAM[];         // Top of BOARD_SDRAM region (0x81E00000)
extern uint32_t __top_NCACHE_REGION[];       // Top of NCACHE_REGION

/* IRQ Dispatcher initialization (registers interrupt callback before any IRQs are enabled) */
extern "C" void CRTOS_IRQDispatcher_Init(void);

/* Stack overflow hook for CRTOS */
static void myStackOverflowHook(const char* taskName, void* taskHandle)
{
    (void)taskHandle;
    PRINTF("Stack overflow in task: %s\r\n", taskName ? taskName : "unknown");
    while (1) {}
}

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/*******************************************************************************
 * DWT Cycle Counter Profiling
 ******************************************************************************/
/* DWT (Data Watchpoint and Trace) registers for cycle counting */
#define DWT_CTRL    (*(volatile uint32_t *)0xE0001000)
#define DWT_CYCCNT  (*(volatile uint32_t *)0xE0001004)
#define DWT_LAR     (*(volatile uint32_t *)0xE0001FB0)
#define CoreDebug_DEMCR (*(volatile uint32_t *)0xE000EDFC)

/* Profiling data structure */
typedef struct {
    uint32_t cycles;       /* Total cycles this frame */
    uint32_t min_cycles;   /* Min cycles observed */
    uint32_t max_cycles;   /* Max cycles observed */
    uint64_t total_cycles; /* Total cycles for averaging */
    uint32_t samples;      /* Number of samples */
} profile_stat_t;

typedef struct {
    profile_stat_t wm_update;
    profile_stat_t wm_animations;
    profile_stat_t wm_simulate;
    profile_stat_t wm_process;
    profile_stat_t wm_draw;
    profile_stat_t vsync_wait;
    profile_stat_t frame_total;
    /* Detailed draw profiling */
    profile_stat_t draw_desktop;
    profile_stat_t draw_windows;
    profile_stat_t draw_startmenu;
    profile_stat_t draw_taskbar;
    profile_stat_t draw_cursor;
    profile_stat_t cache_flush;
    uint32_t cpu_freq_mhz;
} profiler_t;

static profiler_t s_profiler = {0};

/* Initialize DWT cycle counter */
static void DWT_Init(void)
{
    DWT_LAR = 0xC5ACCE55;
    CoreDebug_DEMCR |= (1UL << 24);
    DWT_CYCCNT = 0;
    DWT_CTRL |= (1UL << 0);
    s_profiler.cpu_freq_mhz = CLOCK_GetFreq(kCLOCK_CpuClk) / 1000000;
}

/* Start cycle measurement */
static inline uint32_t DWT_GetCycles(void)
{
    return DWT_CYCCNT;
}

/* Update profile statistics */
static void Profile_Update(profile_stat_t *stat, uint32_t start, uint32_t end)
{
    uint32_t cycles = end - start;
    stat->cycles = cycles;
    stat->total_cycles += cycles;
    stat->samples++;
    if (stat->min_cycles == 0 || cycles < stat->min_cycles) stat->min_cycles = cycles;
    if (cycles > stat->max_cycles) stat->max_cycles = cycles;
}

/* Reset profile stats (call periodically) */
static void Profile_Reset(profile_stat_t *stat)
{
    stat->min_cycles = 0;
    stat->max_cycles = 0;
    stat->total_cycles = 0;
    stat->samples = 0;
}

/* Convert cycles to microseconds */
static inline uint32_t CyclesToUs(uint32_t cycles)
{
    return cycles / s_profiler.cpu_freq_mhz;
}

/* Forward declaration - defined after s_wm and s_milliseconds */
static void Profile_Report(void);

/*******************************************************************************
 * TFTLIB Configuration and Drawing
 ******************************************************************************/
#include "TFTLIB_8BIT.hpp"
/* Font headers are already included via TFTLIB_8BIT.hpp -> gfxfont.h */

/* Local TFTLIB instance - independent of HAL/kernel */
static TFTLIB_8BIT tft;

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#ifndef APP_LCDIF_DATA_BUS
#define APP_LCDIF_DATA_BUS kELCDIF_DataBus16Bit
#endif

/* Window Manager Definitions */
#define WM_TASKBAR_HEIGHT    28
#define WM_SCREEN_W          480
#define WM_SCREEN_H          272

/* Colors (XRGB8888 format) */
#define COLOR_DESKTOP        0xFFE6EBF5   /* Light lavender */
#define COLOR_TASKBAR        0xFFFFFFFF   /* White taskbar */
#define COLOR_TASKBAR_BORDER 0xFFB4B4C3   /* Light border */
#define COLOR_PURPLE         0xFF6450A0   /* Purple (100,80,160) */
#define COLOR_RED            0xFFC83C64   /* Red/Pink (200,60,100) */
#define COLOR_BLUE           0xFF5078C8   /* Blue (80,120,200) */
#define COLOR_WHITE          0xFFFFFFFF
#define COLOR_BLACK          0xFF2D2D37
#define COLOR_GRAY           0xFFB4B4C3
#define COLOR_WINDOW_BG      0xFFF5F5FA
#define COLOR_BUTTON         0xFFE6E6F0
#define COLOR_BUTTON_HOVER   0xFF825AB4
#define COLOR_TEXT           0xFF2D2D37

/* Window IDs */
typedef enum {
    WND_NONE = 0,
    WND_SETTINGS,
    WND_TERMINAL,
    WND_FILES,
    WND_MONITOR,
    WND_ABOUT,
    WND_COUNT
} wnd_id_t;

/* Window structure */
typedef struct {
    wnd_id_t id;
    bool is_open;
    bool is_minimized;
    bool is_maximized;
    int16_t x, y, w, h;
    /* Saved position for restore from maximize */
    int16_t saved_x, saved_y, saved_w, saved_h;
    /* Animation target position */
    float anim_x, anim_y, anim_w, anim_h;
    bool animating;
    const char *title;
} window_t;

/* Drag/Resize state */
typedef enum {
    DRAG_NONE = 0,
    DRAG_MOVE,
    DRAG_RESIZE_BR,  /* Bottom-right resize */
    DRAG_RESIZE_R,   /* Right edge */
    DRAG_RESIZE_B    /* Bottom edge */
} drag_mode_t;

typedef struct {
    drag_mode_t mode;
    int window_id;
    int start_x, start_y;        /* Mouse start position */
    int win_start_x, win_start_y; /* Window start position */
    int win_start_w, win_start_h; /* Window start size */
} drag_state_t;

/* Window Manager state */
typedef struct {
    bool start_menu_open;
    int active_window;
    int hour, minute, second;
    int frame_count;
    
    /* Open windows */
    window_t windows[WND_COUNT];
    
    /* Settings state */
    int brightness;
    bool wifi_enabled;
    bool bluetooth_enabled;
    
    /* Monitor state */
    float cpu_usage;
    float mem_usage;
    float cpu_history[20];
    int history_idx;
    
    /* Performance tracking */
    uint32_t fps;
    uint32_t input_hz;
    uint32_t fps_frame_count;
    uint32_t input_count;
} wm_state_t;

/*******************************************************************************
 * Variables
 ******************************************************************************/
/* Use CRTOS tick count for millisecond timing */
extern volatile uint32_t tickCount;
#define s_milliseconds tickCount

/* CRTOS GUI task handle */
static CRTOS::Task::TaskHandle s_guiTaskHandle = nullptr;

/* Display driver pointer (set after driver loading) */
static CRTOS::Drivers::DriverBase* s_displayDriver = nullptr;

/* Window Manager state */
static wm_state_t s_wm = {0};

/* Drag state for window moving/resizing */
static drag_state_t s_drag = {DRAG_NONE, 0, 0, 0, 0, 0};

/* Print profiling report - implementation (needs s_wm and s_milliseconds) */
static void Profile_Report(void)
{
    static uint32_t last_report = 0;
    
    if (s_milliseconds - last_report < 2000) return;  /* Report every 2 seconds */
    last_report = s_milliseconds;
    
    PRINTF("\r\n=== PROFILE REPORT (us) ====================\r\n");
    PRINTF("Section          Cur      Avg      Min      Max    %%CPU\r\n");
    PRINTF("-----------------------------------------------------\r\n");
    
    #define REPORT_STAT(name, stat) do { \
        uint32_t avg = (stat)->samples ? (uint32_t)((stat)->total_cycles / (stat)->samples) : 0; \
        uint32_t pct = s_profiler.frame_total.cycles ? \
            ((stat)->cycles * 100) / s_profiler.frame_total.cycles : 0; \
        PRINTF("%-16s %4lu     %4lu     %4lu     %4lu    %3lu%%\r\n", \
               name, CyclesToUs((stat)->cycles), CyclesToUs(avg), \
               CyclesToUs((stat)->min_cycles), CyclesToUs((stat)->max_cycles), pct); \
    } while(0)
    
    REPORT_STAT("WM_Update", &s_profiler.wm_update);
    REPORT_STAT("Animations", &s_profiler.wm_animations);
    REPORT_STAT("Simulate", &s_profiler.wm_simulate);
    REPORT_STAT("ProcessInput", &s_profiler.wm_process);
    REPORT_STAT("WM_DrawAll", &s_profiler.wm_draw);
    PRINTF("  --- Draw Breakdown ---\r\n");
    REPORT_STAT("  Desktop", &s_profiler.draw_desktop);
    REPORT_STAT("  Windows", &s_profiler.draw_windows);
    REPORT_STAT("  StartMenu", &s_profiler.draw_startmenu);
    REPORT_STAT("  Taskbar", &s_profiler.draw_taskbar);
    REPORT_STAT("  Cursor", &s_profiler.draw_cursor);
    PRINTF("  ----------------------\r\n");
    REPORT_STAT("CacheFlush", &s_profiler.cache_flush);
    REPORT_STAT("VSync Wait", &s_profiler.vsync_wait);
    REPORT_STAT("FRAME TOTAL", &s_profiler.frame_total);
    
    PRINTF("FPS: %ld, Frame time: %lu us\r\n", s_wm.fps, CyclesToUs(s_profiler.frame_total.cycles));
    PRINTF("=============================================\r\n");
    
    #undef REPORT_STAT
    
    /* Reset stats after report */
    Profile_Reset(&s_profiler.wm_update);
    Profile_Reset(&s_profiler.wm_animations);
    Profile_Reset(&s_profiler.wm_simulate);
    Profile_Reset(&s_profiler.wm_process);
    Profile_Reset(&s_profiler.wm_draw);
    Profile_Reset(&s_profiler.draw_desktop);
    Profile_Reset(&s_profiler.draw_windows);
    Profile_Reset(&s_profiler.draw_startmenu);
    Profile_Reset(&s_profiler.draw_taskbar);
    Profile_Reset(&s_profiler.draw_cursor);
    Profile_Reset(&s_profiler.cache_flush);
    Profile_Reset(&s_profiler.vsync_wait);
    Profile_Reset(&s_profiler.frame_total);
}

/* Animation easing factor (0.0 to 1.0, higher = faster) */
#define ANIM_SPEED 0.25f
#define MIN_WINDOW_W 120
#define MIN_WINDOW_H 80

/* Simulated Input - declared early for dirty rect system */
typedef struct {
    int x, y;
    int pressed;
    int prev_pressed;
    int frame_count;
    int state;
    int sub_state;
} sim_input_t;

static sim_input_t s_input = {240, 136, 0, 0, 0, 0, 0};

/*******************************************************************************
 * Dirty Rectangle System - Only redraw changed areas
 ******************************************************************************/
#define MAX_DIRTY_RECTS 32

typedef struct {
    int16_t x, y, w, h;
} dirty_rect_t;

typedef struct {
    dirty_rect_t rects[MAX_DIRTY_RECTS];
    int count;
    bool full_redraw;  /* Force full screen redraw */
    /* Previous frame state for change detection */
    int16_t prev_cursor_x, prev_cursor_y;
    bool prev_start_menu_open;
    int prev_active_window;
    /* Previous window states */
    int16_t prev_win_x[WND_COUNT];
    int16_t prev_win_y[WND_COUNT];
    int16_t prev_win_w[WND_COUNT];
    int16_t prev_win_h[WND_COUNT];
    bool prev_win_open[WND_COUNT];
    bool prev_win_minimized[WND_COUNT];
    bool prev_win_maximized[WND_COUNT];
} dirty_system_t;

static dirty_system_t s_dirty = {0};

/* Add a dirty rectangle (will be merged/clipped) */
static void Dirty_AddRect(int16_t x, int16_t y, int16_t w, int16_t h)
{
    /* Clip to screen */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > WM_SCREEN_W) w = WM_SCREEN_W - x;
    if (y + h > WM_SCREEN_H) h = WM_SCREEN_H - y;
    if (w <= 0 || h <= 0) return;
    
    /* If already full redraw, skip */
    if (s_dirty.full_redraw) return;
    
    /* Check if this rect can be merged with an existing one */
    for (int i = 0; i < s_dirty.count; i++) {
        dirty_rect_t *r = &s_dirty.rects[i];
        /* Simple overlap/adjacent merge check */
        if (x <= r->x + r->w + 4 && x + w >= r->x - 4 &&
            y <= r->y + r->h + 4 && y + h >= r->y - 4) {
            /* Merge: expand existing rect */
            int16_t nx = (x < r->x) ? x : r->x;
            int16_t ny = (y < r->y) ? y : r->y;
            int16_t nx2 = (x + w > r->x + r->w) ? x + w : r->x + r->w;
            int16_t ny2 = (y + h > r->y + r->h) ? y + h : r->y + r->h;
            r->x = nx; r->y = ny;
            r->w = nx2 - nx; r->h = ny2 - ny;
            return;
        }
    }
    
    /* Add new rect if space available */
    if (s_dirty.count < MAX_DIRTY_RECTS) {
        s_dirty.rects[s_dirty.count].x = x;
        s_dirty.rects[s_dirty.count].y = y;
        s_dirty.rects[s_dirty.count].w = w;
        s_dirty.rects[s_dirty.count].h = h;
        s_dirty.count++;
    } else {
        /* Too many rects, force full redraw */
        s_dirty.full_redraw = true;
    }
}

/* Mark entire screen dirty */
static void Dirty_FullRedraw(void)
{
    s_dirty.full_redraw = true;
}

/* Clear dirty state for next frame */
static void Dirty_Clear(void)
{
    s_dirty.count = 0;
    s_dirty.full_redraw = false;
}

/* Detect changes and mark dirty regions */
static void Dirty_DetectChanges(void)
{
    /* First frame: full redraw */
    static bool first_frame = true;
    if (first_frame) {
        first_frame = false;
        Dirty_FullRedraw();
        return;
    }
    
    /* Cursor moved? */
    if (s_input.x != s_dirty.prev_cursor_x || s_input.y != s_dirty.prev_cursor_y) {
        /* Mark old and new cursor positions */
        Dirty_AddRect(s_dirty.prev_cursor_x - 2, s_dirty.prev_cursor_y - 2, 20, 20);
        Dirty_AddRect(s_input.x - 2, s_input.y - 2, 20, 20);
    }
    
    /* Start menu state changed? */
    if (s_wm.start_menu_open != s_dirty.prev_start_menu_open) {
        Dirty_AddRect(0, WM_SCREEN_H - WM_TASKBAR_HEIGHT - 160, 130, 160);
        Dirty_AddRect(0, WM_SCREEN_H - WM_TASKBAR_HEIGHT, 60, WM_TASKBAR_HEIGHT);
    }
    
    /* Active window changed? */
    if (s_wm.active_window != s_dirty.prev_active_window) {
        /* Mark both old and new active windows */
        if (s_dirty.prev_active_window > 0 && s_dirty.prev_active_window < WND_COUNT) {
            window_t *w = &s_wm.windows[s_dirty.prev_active_window];
            Dirty_AddRect(w->x - 2, w->y - 2, w->w + 4, w->h + 4);
        }
        if (s_wm.active_window > 0 && s_wm.active_window < WND_COUNT) {
            window_t *w = &s_wm.windows[s_wm.active_window];
            Dirty_AddRect(w->x - 2, w->y - 2, w->w + 4, w->h + 4);
        }
        /* Taskbar buttons may change */
        Dirty_AddRect(60, WM_SCREEN_H - WM_TASKBAR_HEIGHT, WM_SCREEN_W - 60, WM_TASKBAR_HEIGHT);
    }
    
    /* Check each window for changes */
    for (int i = 1; i < WND_COUNT; i++) {
        window_t *w = &s_wm.windows[i];
        bool changed = false;
        
        /* Check for any state change */
        if (w->is_open != s_dirty.prev_win_open[i] ||
            w->is_minimized != s_dirty.prev_win_minimized[i] ||
            w->is_maximized != s_dirty.prev_win_maximized[i]) {
            changed = true;
        }
        
        /* Check for position/size change */
        if (w->is_open && !w->is_minimized) {
            int16_t ax = (int16_t)w->anim_x;
            int16_t ay = (int16_t)w->anim_y;
            int16_t aw = (int16_t)w->anim_w;
            int16_t ah = (int16_t)w->anim_h;
            
            if (ax != s_dirty.prev_win_x[i] || ay != s_dirty.prev_win_y[i] ||
                aw != s_dirty.prev_win_w[i] || ah != s_dirty.prev_win_h[i]) {
                changed = true;
                /* Mark old position */
                Dirty_AddRect(s_dirty.prev_win_x[i] - 2, s_dirty.prev_win_y[i] - 2, 
                             s_dirty.prev_win_w[i] + 4, s_dirty.prev_win_h[i] + 4);
            }
        }
        
        if (changed) {
            /* Mark new position */
            int16_t ax = (int16_t)w->anim_x;
            int16_t ay = (int16_t)w->anim_y;
            int16_t aw = (int16_t)w->anim_w;
            int16_t ah = (int16_t)w->anim_h;
            Dirty_AddRect(ax - 2, ay - 2, aw + 4, ah + 4);
            
            /* Taskbar buttons may change */
            Dirty_AddRect(60, WM_SCREEN_H - WM_TASKBAR_HEIGHT, WM_SCREEN_W - 150, WM_TASKBAR_HEIGHT);
        }
    }
    
    /* Windows with animating content (Monitor) always need redraw */
    if (s_wm.windows[WND_MONITOR].is_open && !s_wm.windows[WND_MONITOR].is_minimized) {
        window_t *w = &s_wm.windows[WND_MONITOR];
        Dirty_AddRect((int16_t)w->anim_x, (int16_t)w->anim_y, (int16_t)w->anim_w, (int16_t)w->anim_h);
    }
    
    /* Taskbar clock always updates */
    Dirty_AddRect(WM_SCREEN_W - 55, WM_SCREEN_H - WM_TASKBAR_HEIGHT, 55, WM_TASKBAR_HEIGHT);
}

/* Save current state for next frame comparison */
static void Dirty_SaveState(void)
{
    s_dirty.prev_cursor_x = s_input.x;
    s_dirty.prev_cursor_y = s_input.y;
    s_dirty.prev_start_menu_open = s_wm.start_menu_open;
    s_dirty.prev_active_window = s_wm.active_window;
    
    for (int i = 1; i < WND_COUNT; i++) {
        window_t *w = &s_wm.windows[i];
        s_dirty.prev_win_x[i] = (int16_t)w->anim_x;
        s_dirty.prev_win_y[i] = (int16_t)w->anim_y;
        s_dirty.prev_win_w[i] = (int16_t)w->anim_w;
        s_dirty.prev_win_h[i] = (int16_t)w->anim_h;
        s_dirty.prev_win_open[i] = w->is_open;
        s_dirty.prev_win_minimized[i] = w->is_minimized;
        s_dirty.prev_win_maximized[i] = w->is_maximized;
    }
}

/*******************************************************************************
 * Forward Declarations
 ******************************************************************************/
static void WM_CloseAllWindows(void);
static void WM_OpenWindow(wnd_id_t id);
static void WM_DrawAll(uint32_t *fb);

/*******************************************************************************
 * Simulated Input State Machine
 ******************************************************************************/
static void WM_SimulateInput(sim_input_t *inp)
{
    const int speed = 4;
    
    if (inp->frame_count < 10000) inp->frame_count++;
    inp->prev_pressed = inp->pressed;
    
    #define MOVE_TO(tx, ty) do { \
        if (inp->x < (tx)) inp->x += speed; \
        else if (inp->x > (tx)) inp->x -= speed; \
        if (inp->y < (ty)) inp->y += speed; \
        else if (inp->y > (ty)) inp->y -= speed; \
        if (abs(inp->x - (tx)) <= speed) inp->x = (tx); \
        if (abs(inp->y - (ty)) <= speed) inp->y = (ty); \
    } while(0)
    
    #define AT_TARGET(tx, ty) (inp->x == (tx) && inp->y == (ty))
    #define WAIT(frames) (inp->frame_count > (frames))
    #define CLICK() do { inp->pressed = 1; } while(0)
    #define RELEASE() do { inp->pressed = 0; } while(0)
    #define NEXT_STATE() do { inp->state++; inp->frame_count = 0; inp->sub_state = 0; } while(0)
    #define NEXT_SUB() do { inp->sub_state++; inp->frame_count = 0; } while(0)
    #define RESET_ALL() do { \
        inp->state = 0; inp->frame_count = 0; inp->sub_state = 0; \
        inp->x = 240; inp->y = 136; inp->pressed = 0; \
        WM_CloseAllWindows(); \
    } while(0)
    
    /* Button positions */
    #define START_X 30
    #define START_Y 260
    #define MENU_X 60
    #define SETTINGS_Y 110
    #define TERMINAL_Y 138
    #define FILES_Y 166
    #define MONITOR_Y 194
    #define ABOUT_Y 222
    
    /* Window control button positions (relative to window) */
    /* Maximize = w->x + w->w - 38, Minimize = w->x + w->w - 56 */
    
    switch (inp->state) {
        /* --- Open Settings Window --- */
        case 0: MOVE_TO(START_X, START_Y);
            if (AT_TARGET(START_X, START_Y) && WAIT(30)) NEXT_STATE();
            break;
        case 1: if (inp->sub_state == 0) { CLICK(); NEXT_SUB(); }
            else if (WAIT(10)) { RELEASE(); NEXT_STATE(); }
            break;
        case 2: if (WAIT(20)) { MOVE_TO(MENU_X, SETTINGS_Y);
            if (AT_TARGET(MENU_X, SETTINGS_Y)) NEXT_STATE(); }
            break;
        case 3: if (inp->sub_state == 0) { CLICK(); NEXT_SUB(); }
            else if (WAIT(10)) { RELEASE(); NEXT_STATE(); }
            break;
        case 4: if (WAIT(30)) NEXT_STATE(); break;
        
        /* --- Drag Settings Window (hold and move) --- */
        case 5: { /* Move to Settings title bar */
            window_t *w = &s_wm.windows[WND_SETTINGS]; /* Settings = 1 */
            int titleX = w->x + w->w / 2;
            int titleY = w->y + 12;
            MOVE_TO(titleX, titleY);
            if (AT_TARGET(titleX, titleY)) NEXT_STATE();
        } break;
        case 6: /* Press and hold for drag */
            if (inp->sub_state == 0) { CLICK(); NEXT_SUB(); }
            else if (WAIT(5)) NEXT_STATE();
            break;
        case 7: /* Drag to new position (smooth drag while holding) */
            MOVE_TO(300, 100);
            if (AT_TARGET(300, 100) && WAIT(10)) NEXT_STATE();
            break;
        case 8: /* Release to drop */
            RELEASE();
            if (WAIT(20)) NEXT_STATE();
            break;
        
        /* --- Maximize Settings Window --- */
        case 9: { /* Move to maximize button */
            window_t *w = &s_wm.windows[WND_SETTINGS];
            /* Maximize button is at w->x + w->w - 48, center of 20px button */
            int maxBtnX = w->x + w->w - 48 + 10;
            int maxBtnY = w->y + 2 + 10;
            MOVE_TO(maxBtnX, maxBtnY);
            if (AT_TARGET(maxBtnX, maxBtnY)) NEXT_STATE();
        } break;
        case 10: if (inp->sub_state == 0) { CLICK(); NEXT_SUB(); }
            else if (WAIT(10)) { RELEASE(); NEXT_STATE(); }
            break;
        case 11: if (WAIT(40)) NEXT_STATE(); break; /* Wait for animation */
        
        /* --- Restore Settings Window --- */
        case 12: { /* Move to maximize button again to restore */
            window_t *w = &s_wm.windows[WND_SETTINGS];
            int maxBtnX = w->x + w->w - 48 + 10;
            int maxBtnY = w->y + 2 + 10;
            MOVE_TO(maxBtnX, maxBtnY);
            if (AT_TARGET(maxBtnX, maxBtnY)) NEXT_STATE();
        } break;
        case 13: if (inp->sub_state == 0) { CLICK(); NEXT_SUB(); }
            else if (WAIT(10)) { RELEASE(); NEXT_STATE(); }
            break;
        case 14: if (WAIT(40)) NEXT_STATE(); break;
        
        /* --- Minimize Settings Window --- */
        case 15: { /* Move to minimize button */
            window_t *w = &s_wm.windows[WND_SETTINGS];
            /* Minimize button is at w->x + w->w - 72, center of 20px button */
            int minBtnX = w->x + w->w - 72 + 10;
            int minBtnY = w->y + 2 + 10;
            MOVE_TO(minBtnX, minBtnY);
            if (AT_TARGET(minBtnX, minBtnY)) NEXT_STATE();
        } break;
        case 16: if (inp->sub_state == 0) { CLICK(); NEXT_SUB(); }
            else if (WAIT(10)) { RELEASE(); NEXT_STATE(); }
            break;
        case 17: if (WAIT(30)) NEXT_STATE(); break;
        
        /* --- Open Terminal Window --- */
        case 18: MOVE_TO(START_X, START_Y);
            if (AT_TARGET(START_X, START_Y) && WAIT(20)) NEXT_STATE();
            break;
        case 19: if (inp->sub_state == 0) { CLICK(); NEXT_SUB(); }
            else if (WAIT(10)) { RELEASE(); NEXT_STATE(); }
            break;
        case 20: if (WAIT(20)) { MOVE_TO(MENU_X, TERMINAL_Y);
            if (AT_TARGET(MENU_X, TERMINAL_Y)) NEXT_STATE(); }
            break;
        case 21: if (inp->sub_state == 0) { CLICK(); NEXT_SUB(); }
            else if (WAIT(10)) { RELEASE(); NEXT_STATE(); }
            break;
        case 22: if (WAIT(30)) NEXT_STATE(); break;
        
        /* --- Resize Terminal Window (drag resize handle) --- */
        case 23: { /* Move to resize handle (bottom-right corner) */
            window_t *w = &s_wm.windows[WND_TERMINAL]; /* Terminal = 2 */
            int resizeX = w->x + w->w - 8;
            int resizeY = w->y + w->h - 8;
            MOVE_TO(resizeX, resizeY);
            if (AT_TARGET(resizeX, resizeY)) NEXT_STATE();
        } break;
        case 24: /* Press and hold for resize */
            if (inp->sub_state == 0) { CLICK(); NEXT_SUB(); }
            else if (WAIT(5)) NEXT_STATE();
            break;
        case 25: { /* Drag to resize larger */
            window_t *w = &s_wm.windows[WND_TERMINAL];
            int newX = w->x + 200;
            int newY = w->y + 140;
            MOVE_TO(newX, newY);
            if (AT_TARGET(newX, newY) && WAIT(10)) NEXT_STATE();
        } break;
        case 26: /* Release */
            RELEASE();
            if (WAIT(40)) NEXT_STATE();
            break;
        
        /* --- Close Terminal and open remaining windows --- */
        case 27: { /* Close terminal */
            window_t *w = &s_wm.windows[WND_TERMINAL];
            /* Close button is at w->x + w->w - 24, center of 20px button */
            int closeX = w->x + w->w - 24 + 10;
            int closeY = w->y + 2 + 10;
            MOVE_TO(closeX, closeY);
            if (AT_TARGET(closeX, closeY)) NEXT_STATE();
        } break;
        case 28: if (inp->sub_state == 0) { CLICK(); NEXT_SUB(); }
            else if (WAIT(10)) { RELEASE(); NEXT_STATE(); }
            break;
        case 29: if (WAIT(20)) NEXT_STATE(); break;
        
        /* --- Open and drag Monitor window --- */
        case 30: MOVE_TO(START_X, START_Y);
            if (AT_TARGET(START_X, START_Y) && WAIT(20)) NEXT_STATE();
            break;
        case 31: if (inp->sub_state == 0) { CLICK(); NEXT_SUB(); }
            else if (WAIT(10)) { RELEASE(); NEXT_STATE(); }
            break;
        case 32: if (WAIT(20)) { MOVE_TO(MENU_X, MONITOR_Y);
            if (AT_TARGET(MENU_X, MONITOR_Y)) NEXT_STATE(); }
            break;
        case 33: if (inp->sub_state == 0) { CLICK(); NEXT_SUB(); }
            else if (WAIT(10)) { RELEASE(); NEXT_STATE(); }
            break;
        case 34: if (WAIT(30)) NEXT_STATE(); break;
        
        /* Drag Monitor window */
        case 35: { 
            window_t *w = &s_wm.windows[WND_MONITOR]; /* Monitor = 4 */
            MOVE_TO(w->x + 60, w->y + 12);
            if (AT_TARGET(w->x + 60, w->y + 12)) NEXT_STATE();
        } break;
        case 36: if (inp->sub_state == 0) { CLICK(); NEXT_SUB(); }
            else if (WAIT(5)) NEXT_STATE();
            break;
        case 37: MOVE_TO(150, 50);
            if (AT_TARGET(150, 50)) NEXT_STATE();
            break;
        case 38: RELEASE(); if (WAIT(60)) NEXT_STATE(); break;
        
        /* --- Open About window --- */
        case 39: MOVE_TO(START_X, START_Y);
            if (AT_TARGET(START_X, START_Y) && WAIT(20)) NEXT_STATE();
            break;
        case 40: if (inp->sub_state == 0) { CLICK(); NEXT_SUB(); }
            else if (WAIT(10)) { RELEASE(); NEXT_STATE(); }
            break;
        case 41: if (WAIT(20)) { MOVE_TO(MENU_X, ABOUT_Y);
            if (AT_TARGET(MENU_X, ABOUT_Y)) NEXT_STATE(); }
            break;
        case 42: if (inp->sub_state == 0) { CLICK(); NEXT_SUB(); }
            else if (WAIT(10)) { RELEASE(); NEXT_STATE(); }
            break;
        case 43: if (WAIT(90)) RESET_ALL(); break;
            
        default: RESET_ALL(); break;
    }
    
    #undef MOVE_TO
    #undef AT_TARGET
    #undef WAIT
    #undef CLICK
    #undef RELEASE
    #undef NEXT_STATE
    #undef NEXT_SUB
    #undef RESET_ALL
    #undef START_X
    #undef START_Y
    #undef MENU_X
    #undef SETTINGS_Y
    #undef TERMINAL_Y
    #undef FILES_Y
    #undef MONITOR_Y
    #undef ABOUT_Y
}

/*******************************************************************************
 * Drawing Helpers - Using TFTLIB (optimized with 64-bit writes)
 ******************************************************************************/
static inline void DrawPixel(uint32_t *fb, int x, int y, uint32_t color)
{
    (void)fb;  /* TFTLIB uses its internal framebuffer pointer */
    tft.drawPixel(x, y, color);
}

static void DrawRect(uint32_t *fb, int x, int y, int w, int h, uint32_t color, bool fill)
{
    (void)fb;  /* TFTLIB uses its internal framebuffer pointer */
    if (fill) {
        /* TFTLIB fillRect is now optimized with 64-bit writes */
        tft.fillRect(x, y, w, h, color);
    } else {
        tft.drawRect(x, y, w, h, color);
    }
}

/* Draw filled rectangle with alpha blending (alpha: 0=transparent, 255=opaque) */
static void DrawRectAlpha(uint32_t *fb, int x, int y, int w, int h, uint32_t color, uint8_t alpha)
{
    (void)fb;
    tft.fillRectAlpha(x, y, w, h, color, alpha);
}

/* Draw rounded rectangle (r = corner radius) */
static void DrawRoundRect(uint32_t *fb, int x, int y, int w, int h, int r, uint32_t color, bool fill)
{
    (void)fb;  /* TFTLIB uses its internal framebuffer pointer */
    if (fill) {
        tft.fillRoundRect(x, y, w, h, r, color);
    } else {
        tft.drawRoundRect(x, y, w, h, r, color);
    }
}

/* Draw rounded rectangle with alpha blending */
static void DrawRoundRectAlpha(uint32_t *fb, int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha)
{
    (void)fb;
    tft.fillAlphaRoundRect(x, y, w, h, r, color, alpha);
}

static void DrawText(uint32_t *fb, int x, int y, const char *text, uint32_t color)
{
    (void)fb;  /* TFTLIB uses its internal framebuffer pointer */
    tft.setTextColor(color, color);  /* Same fg/bg = transparent background */
    tft.drawString(text, x, y);  /* TFTLIB handles baseline internally for GFX fonts */
}

/* Draw text clipped to maxWidth - truncates with ellipsis if too long */
static void DrawTextClipped(uint32_t *fb, int x, int y, const char *text, uint32_t color, int maxWidth)
{
    (void)fb;
    tft.setTextColor(color, color);
    
    int tw = tft.textWidth(text);
    if (tw <= maxWidth) {
        tft.drawString(text, x, y);
    } else {
        /* Truncate text to fit */
        char buf[32];
        int len = strlen(text);
        int i;
        for (i = len - 1; i > 0; i--) {
            strncpy(buf, text, i);
            buf[i] = '\0';
            if (tft.textWidth(buf) <= maxWidth - 6) {  /* Reserve space for .. */
                strcat(buf, "..");
                break;
            }
        }
        if (i > 0) {
            tft.drawString(buf, x, y);
        }
    }
}

/* Draw text centered within a given width */
static void DrawTextCentered(uint32_t *fb, int x, int y, int width, const char *text, uint32_t color)
{
    (void)fb;
    int tw = tft.textWidth(text);
    int cx = x + (width - tw) / 2;
    tft.setTextColor(color, color);  /* Same fg/bg = transparent background */
    tft.drawString(text, cx, y);
}

/*******************************************************************************
 * Window Manager - Core Functions
 ******************************************************************************/
static void WM_InitWindow(window_t *w, wnd_id_t id, int16_t x, int16_t y, int16_t width, int16_t h, const char *title)
{
    w->id = id;
    w->is_open = false;
    w->is_minimized = false;
    w->is_maximized = false;
    w->x = x; w->y = y; w->w = width; w->h = h;
    w->saved_x = x; w->saved_y = y; w->saved_w = width; w->saved_h = h;
    w->anim_x = x; w->anim_y = y; w->anim_w = width; w->anim_h = h;
    w->animating = false;
    w->title = title;
}

static void WM_Init(void)
{
    memset(&s_wm, 0, sizeof(s_wm));
    memset(&s_drag, 0, sizeof(s_drag));
    s_wm.hour = 10;
    s_wm.minute = 30;
    s_wm.brightness = 80;
    s_wm.wifi_enabled = true;
    s_wm.cpu_usage = 25.0f;
    s_wm.mem_usage = 48.0f;
    s_wm.active_window = -1;
    
    /* Initialize window definitions */
    WM_InitWindow(&s_wm.windows[WND_SETTINGS], WND_SETTINGS, 20, 5, 200, 160, "Settings");
    WM_InitWindow(&s_wm.windows[WND_TERMINAL], WND_TERMINAL, 35, 15, 200, 160, "Terminal");
    WM_InitWindow(&s_wm.windows[WND_FILES], WND_FILES, 50, 25, 200, 160, "Files");
    WM_InitWindow(&s_wm.windows[WND_MONITOR], WND_MONITOR, 65, 35, 200, 180, "Monitor");
    WM_InitWindow(&s_wm.windows[WND_ABOUT], WND_ABOUT, 80, 45, 200, 160, "About");
}

static void WM_CloseAllWindows(void)
{
    /* Reset all windows to initial state */
    WM_InitWindow(&s_wm.windows[WND_SETTINGS], WND_SETTINGS, 20, 5, 200, 160, "Settings");
    WM_InitWindow(&s_wm.windows[WND_TERMINAL], WND_TERMINAL, 35, 15, 200, 160, "Terminal");
    WM_InitWindow(&s_wm.windows[WND_FILES], WND_FILES, 50, 25, 200, 160, "Files");
    WM_InitWindow(&s_wm.windows[WND_MONITOR], WND_MONITOR, 65, 35, 200, 180, "Monitor");
    WM_InitWindow(&s_wm.windows[WND_ABOUT], WND_ABOUT, 80, 45, 200, 160, "About");
    
    s_drag.mode = DRAG_NONE;
    s_wm.start_menu_open = false;
    s_wm.active_window = -1;
}

static void WM_OpenWindow(wnd_id_t id)
{
    if (id >= WND_COUNT) return;
    
    s_wm.windows[id].is_open = true;
    s_wm.windows[id].is_minimized = false;
    s_wm.active_window = id;
    s_wm.start_menu_open = false;
}

/* Start smooth animation to target position/size */
static void WM_AnimateWindow(window_t *w, int16_t tx, int16_t ty, int16_t tw, int16_t th)
{
    w->anim_x = (float)w->x;
    w->anim_y = (float)w->y;
    w->anim_w = (float)w->w;
    w->anim_h = (float)w->h;
    w->x = tx;
    w->y = ty;
    w->w = tw;
    w->h = th;
    w->animating = true;
}

/* Maximize window */
static void WM_MaximizeWindow(window_t *w)
{
    if (w->is_maximized) {
        /* Restore */
        w->is_maximized = false;
        WM_AnimateWindow(w, w->saved_x, w->saved_y, w->saved_w, w->saved_h);
    } else {
        /* Save current position and maximize */
        w->saved_x = w->x;
        w->saved_y = w->y;
        w->saved_w = w->w;
        w->saved_h = w->h;
        w->is_maximized = true;
        WM_AnimateWindow(w, 0, 0, WM_SCREEN_W, WM_SCREEN_H - WM_TASKBAR_HEIGHT);
    }
}

/* Minimize window */
static void WM_MinimizeWindow(window_t *w)
{
    w->is_minimized = true;
}

/* Update window animations */
static void WM_UpdateAnimations(void)
{
    for (int i = 1; i < WND_COUNT; i++) {
        window_t *w = &s_wm.windows[i];
        if (w->animating) {
            /* Smooth interpolation towards target */
            float dx = (float)w->x - w->anim_x;
            float dy = (float)w->y - w->anim_y;
            float dw = (float)w->w - w->anim_w;
            float dh = (float)w->h - w->anim_h;
            
            w->anim_x += dx * ANIM_SPEED;
            w->anim_y += dy * ANIM_SPEED;
            w->anim_w += dw * ANIM_SPEED;
            w->anim_h += dh * ANIM_SPEED;
            
            /* Check if animation is complete (close enough) */
            if (fabsf(dx) < 1.0f && fabsf(dy) < 1.0f && 
                fabsf(dw) < 1.0f && fabsf(dh) < 1.0f) {
                w->anim_x = (float)w->x;
                w->anim_y = (float)w->y;
                w->anim_w = (float)w->w;
                w->anim_h = (float)w->h;
                w->animating = false;
            }
        }
    }
}

static void WM_Update(void)
{
    static uint32_t last_fps_time = 0;
    
    /* FPS tracking - measure frames per actual second */
    s_wm.fps_frame_count++;
    s_wm.input_count++;
    
    if (s_milliseconds - last_fps_time >= 1000) {
        s_wm.fps = s_wm.fps_frame_count;
        s_wm.input_hz = s_wm.input_count;
        s_wm.fps_frame_count = 0;
        s_wm.input_count = 0;
        last_fps_time = s_milliseconds;
    }
    
    /* Clock update */
    s_wm.frame_count++;
    if (s_wm.frame_count >= 60) {
        s_wm.frame_count = 0;
        s_wm.second++;
        if (s_wm.second >= 60) {
            s_wm.second = 0;
            s_wm.minute++;
            if (s_wm.minute >= 60) {
                s_wm.minute = 0;
                s_wm.hour = (s_wm.hour + 1) % 24;
            }
        }
    }
    
    /* CPU simulation */
    static int cpu_counter = 0;
    if (++cpu_counter >= 10) {
        cpu_counter = 0;
        s_wm.cpu_usage = 25.0f + (float)((s_wm.history_idx * 7 + s_wm.second * 3) % 35);
        s_wm.cpu_history[s_wm.history_idx] = s_wm.cpu_usage;
        s_wm.history_idx = (s_wm.history_idx + 1) % 20;
    }
}

/*******************************************************************************
 * Input Processing
 ******************************************************************************/
static bool PointInRect(int px, int py, int x, int y, int w, int h)
{
    return (px >= x && px < x + w && py >= y && py < y + h);
}

/* Handle continuous drag/resize */
static void WM_ProcessDrag(sim_input_t *inp)
{
    if (!inp->pressed) {
        s_drag.mode = DRAG_NONE;
        return;
    }
    
    if (s_drag.mode == DRAG_NONE) return;
    
    window_t *w = &s_wm.windows[s_drag.window_id];
    int dx = inp->x - s_drag.start_x;
    int dy = inp->y - s_drag.start_y;
    
    switch (s_drag.mode) {
        case DRAG_MOVE:
            w->x = s_drag.win_start_x + dx;
            w->y = s_drag.win_start_y + dy;
            /* Clamp to screen */
            if (w->x < 0) w->x = 0;
            if (w->y < 0) w->y = 0;
            if (w->x + w->w > WM_SCREEN_W) w->x = WM_SCREEN_W - w->w;
            if (w->y + 24 > WM_SCREEN_H - WM_TASKBAR_HEIGHT) w->y = WM_SCREEN_H - WM_TASKBAR_HEIGHT - 24;
            /* Update animation position to follow */
            w->anim_x = (float)w->x;
            w->anim_y = (float)w->y;
            /* Clear maximized if moving */
            if (w->is_maximized) {
                w->is_maximized = false;
                w->w = w->saved_w;
                w->h = w->saved_h;
                w->anim_w = (float)w->w;
                w->anim_h = (float)w->h;
            }
            break;
            
        case DRAG_RESIZE_BR:
            w->w = s_drag.win_start_w + dx;
            w->h = s_drag.win_start_h + dy;
            if (w->w < MIN_WINDOW_W) w->w = MIN_WINDOW_W;
            if (w->h < MIN_WINDOW_H) w->h = MIN_WINDOW_H;
            if (w->x + w->w > WM_SCREEN_W) w->w = WM_SCREEN_W - w->x;
            if (w->y + w->h > WM_SCREEN_H - WM_TASKBAR_HEIGHT) w->h = WM_SCREEN_H - WM_TASKBAR_HEIGHT - w->y;
            w->anim_w = (float)w->w;
            w->anim_h = (float)w->h;
            w->is_maximized = false;
            break;
            
        case DRAG_RESIZE_R:
            w->w = s_drag.win_start_w + dx;
            if (w->w < MIN_WINDOW_W) w->w = MIN_WINDOW_W;
            if (w->x + w->w > WM_SCREEN_W) w->w = WM_SCREEN_W - w->x;
            w->anim_w = (float)w->w;
            w->is_maximized = false;
            break;
            
        case DRAG_RESIZE_B:
            w->h = s_drag.win_start_h + dy;
            if (w->h < MIN_WINDOW_H) w->h = MIN_WINDOW_H;
            if (w->y + w->h > WM_SCREEN_H - WM_TASKBAR_HEIGHT) w->h = WM_SCREEN_H - WM_TASKBAR_HEIGHT - w->y;
            w->anim_h = (float)w->h;
            w->is_maximized = false;
            break;
            
        default:
            break;
    }
}

static void WM_ProcessInput(sim_input_t *inp)
{
    bool clicked = (inp->pressed && !inp->prev_pressed);
    bool released = (!inp->pressed && inp->prev_pressed);
    
    /* Handle ongoing drag/resize */
    if (s_drag.mode != DRAG_NONE) {
        WM_ProcessDrag(inp);
        if (released) {
            s_drag.mode = DRAG_NONE;
        }
        return;
    }
    
    if (!clicked) return;
    
    /* Check Start button */
    if (PointInRect(inp->x, inp->y, 0, WM_SCREEN_H - WM_TASKBAR_HEIGHT, 60, WM_TASKBAR_HEIGHT)) {
        s_wm.start_menu_open = !s_wm.start_menu_open;
        return;
    }
    
    /* Check Start Menu items */
    if (s_wm.start_menu_open) {
        int menuX = 0, menuY = WM_SCREEN_H - WM_TASKBAR_HEIGHT - 160;
        int menuW = 130, itemH = 30;
        
        if (PointInRect(inp->x, inp->y, menuX, menuY, menuW, 160)) {
            int idx = (inp->y - menuY - 6) / itemH;
            if (idx == 0) WM_OpenWindow(WND_SETTINGS);
            else if (idx == 1) WM_OpenWindow(WND_TERMINAL);
            else if (idx == 2) WM_OpenWindow(WND_FILES);
            else if (idx == 3) WM_OpenWindow(WND_MONITOR);
            else if (idx == 4) WM_OpenWindow(WND_ABOUT);
            return;
        }
    }
    
    /* Check windows (from front to back = active first) */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 1; i < WND_COUNT; i++) {
            /* First pass: active window only, second pass: others */
            if (pass == 0 && i != s_wm.active_window) continue;
            if (pass == 1 && i == s_wm.active_window) continue;
            
            window_t *w = &s_wm.windows[i];
            if (!w->is_open || w->is_minimized) continue;
            
            /* Close button (X) */
            if (PointInRect(inp->x, inp->y, w->x + w->w - 24, w->y + 2, 20, 20)) {
                w->is_open = false;
                w->is_maximized = false;
                if (s_wm.active_window == i) s_wm.active_window = -1;
                return;
            }
            
            /* Maximize button */
            if (PointInRect(inp->x, inp->y, w->x + w->w - 48, w->y + 2, 20, 20)) {
                WM_MaximizeWindow(w);
                s_wm.active_window = i;
                return;
            }
            
            /* Minimize button */
            if (PointInRect(inp->x, inp->y, w->x + w->w - 72, w->y + 2, 20, 20)) {
                WM_MinimizeWindow(w);
                if (s_wm.active_window == i) s_wm.active_window = -1;
                return;
            }
            
            /* Title bar drag (excluding buttons) */
            if (PointInRect(inp->x, inp->y, w->x, w->y, w->w - 75, 24)) {
                s_drag.mode = DRAG_MOVE;
                s_drag.window_id = i;
                s_drag.start_x = inp->x;
                s_drag.start_y = inp->y;
                s_drag.win_start_x = w->x;
                s_drag.win_start_y = w->y;
                s_wm.active_window = i;
                return;
            }
            
            /* Resize handles (only if not maximized) */
            if (!w->is_maximized) {
                /* Bottom-right corner */
                if (PointInRect(inp->x, inp->y, w->x + w->w - 12, w->y + w->h - 12, 12, 12)) {
                    s_drag.mode = DRAG_RESIZE_BR;
                    s_drag.window_id = i;
                    s_drag.start_x = inp->x;
                    s_drag.start_y = inp->y;
                    s_drag.win_start_w = w->w;
                    s_drag.win_start_h = w->h;
                    s_wm.active_window = i;
                    return;
                }
                
                /* Right edge */
                if (PointInRect(inp->x, inp->y, w->x + w->w - 4, w->y + 24, 4, w->h - 36)) {
                    s_drag.mode = DRAG_RESIZE_R;
                    s_drag.window_id = i;
                    s_drag.start_x = inp->x;
                    s_drag.start_y = inp->y;
                    s_drag.win_start_w = w->w;
                    s_wm.active_window = i;
                    return;
                }
                
                /* Bottom edge */
                if (PointInRect(inp->x, inp->y, w->x, w->y + w->h - 4, w->w - 12, 4)) {
                    s_drag.mode = DRAG_RESIZE_B;
                    s_drag.window_id = i;
                    s_drag.start_x = inp->x;
                    s_drag.start_y = inp->y;
                    s_drag.win_start_h = w->h;
                    s_wm.active_window = i;
                    return;
                }
            }
            
            /* Click anywhere in window to activate */
            if (PointInRect(inp->x, inp->y, w->x, w->y, w->w, w->h)) {
                s_wm.active_window = i;
                return;
            }
        }
    }
    
    /* Check taskbar window buttons */
    int btnX = 65;
    for (int i = 1; i < WND_COUNT; i++) {
        if (s_wm.windows[i].is_open) {
            if (PointInRect(inp->x, inp->y, btnX, WM_SCREEN_H - WM_TASKBAR_HEIGHT, 55, WM_TASKBAR_HEIGHT)) {
                if (s_wm.windows[i].is_minimized) {
                    s_wm.windows[i].is_minimized = false;
                    s_wm.active_window = i;
                } else if (s_wm.active_window == i) {
                    s_wm.windows[i].is_minimized = true;
                } else {
                    s_wm.active_window = i;
                }
                return;
            }
            btnX += 58;
        }
    }
    
    /* Close menu if clicking elsewhere */
    s_wm.start_menu_open = false;
}

/*******************************************************************************
 * Drawing - Windows
 ******************************************************************************/
#define WND_CORNER_RADIUS  6   /* Corner radius for windows */
#define BTN_CORNER_RADIUS  4   /* Corner radius for buttons */

static void WM_DrawWindow(uint32_t *fb, window_t *w)
{
    if (!w->is_open || w->is_minimized) return;
    
    /* Use animation position for smooth movement */
    int16_t dx = w->animating ? (int16_t)w->anim_x : w->x;
    int16_t dy = w->animating ? (int16_t)w->anim_y : w->y;
    int16_t dw = w->animating ? (int16_t)w->anim_w : w->w;
    int16_t dh = w->animating ? (int16_t)w->anim_h : w->h;
    
    int r = WND_CORNER_RADIUS;
    int br = BTN_CORNER_RADIUS;
    
    /* Draw window background with rounded corners */
    DrawRoundRect(fb, dx, dy, dw, dh, r, COLOR_WINDOW_BG, true);
    
    /* Header with rounded top corners only - draw as rounded rect, then cover bottom with square */
    DrawRoundRect(fb, dx, dy, dw, 24 + r, r, COLOR_PURPLE, true);
    DrawRect(fb, dx, dy + 24, dw, r, COLOR_PURPLE, true);  /* Cover bottom corners */
    
    /* Title text */
    DrawText(fb, dx + 4, dy + 4, w->title, COLOR_WHITE);
    
    /* Window control buttons - draw all three with rounded corners */
    /* Close (X), Maximize, Minimize - from right to left */
    int bx = dx + dw - 24;
    DrawRoundRect(fb, bx, dy + 2, 20, 20, br, 0xFFE04040, true);  /* Close */
    DrawText(fb, bx + 4, dy + 3, "X", COLOR_WHITE);
    
    bx -= 24;
    uint32_t maxColor = w->is_maximized ? 0xFF40A040 : 0xFF4080E0;
    DrawRoundRect(fb, bx, dy + 2, 20, 20, br, maxColor, true);    /* Maximize */
    DrawRoundRect(fb, bx + 4, dy + 6, 12, 12, 2, COLOR_WHITE, false);
    
    bx -= 24;
    DrawRoundRect(fb, bx, dy + 2, 20, 20, br, 0xFFE0A040, true);  /* Minimize */
    DrawRect(fb, bx + 4, dy + 14, 12, 3, COLOR_WHITE, true);
    
    /* Border with rounded corners */
    DrawRoundRect(fb, dx, dy, dw, dh, r, COLOR_GRAY, false);
    
    /* OPTIMIZATION: Simpler resize handle using small rectangles instead of pixels */
    if (!w->is_maximized) {
        /* Two small lines for resize grip */
        DrawRect(fb, dx + dw - 10, dy + dh - 3, 8, 1, COLOR_GRAY, true);
        DrawRect(fb, dx + dw - 3, dy + dh - 10, 1, 8, COLOR_GRAY, true);
        DrawRect(fb, dx + dw - 7, dy + dh - 6, 5, 1, COLOR_GRAY, true);
        DrawRect(fb, dx + dw - 6, dy + dh - 7, 1, 5, COLOR_GRAY, true);
    }
}

/* Helper to get window drawing position (for content) */
static void WM_GetDrawPos(window_t *w, int16_t *x, int16_t *y, int16_t *width, int16_t *height)
{
    *x = w->animating ? (int16_t)w->anim_x : w->x;
    *y = w->animating ? (int16_t)w->anim_y : w->y;
    *width = w->animating ? (int16_t)w->anim_w : w->w;
    *height = w->animating ? (int16_t)w->anim_h : w->h;
}

static void WM_DrawSettingsContent(uint32_t *fb, window_t *w)
{
    if (!w->is_open || w->is_minimized) return;
    
    int16_t wx, wy, ww, wh;
    WM_GetDrawPos(w, &wx, &wy, &ww, &wh);
    
    int cx = wx + 10;
    int cy = wy + 30;
    char buf[32];
    
    DrawText(fb, cx, cy, "Theme: Light", COLOR_TEXT);
    cy += 22;
    
    sprintf(buf, "Brightness: %d%%", s_wm.brightness);
    DrawText(fb, cx, cy, buf, COLOR_TEXT);
    /* Progress bar with rounded corners */
    DrawRoundRect(fb, cx, cy + 18, 100, 8, 3, COLOR_GRAY, false);
    int barWidth = (s_wm.brightness * 96) / 100;
    if (barWidth > 4) {  /* Only draw if wide enough for rounded corners */
        DrawRoundRect(fb, cx + 2, cy + 20, barWidth, 4, 2, COLOR_BLUE, true);
    }
    cy += 34;
    
    DrawText(fb, cx, cy, s_wm.wifi_enabled ? "[x] WiFi" : "[ ] WiFi", COLOR_TEXT);
    cy += 20;
    DrawText(fb, cx, cy, s_wm.bluetooth_enabled ? "[x] Bluetooth" : "[ ] Bluetooth", COLOR_TEXT);
}

static void WM_DrawTerminalContent(uint32_t *fb, window_t *w)
{
    if (!w->is_open || w->is_minimized) return;
    
    int16_t wx, wy, ww, wh;
    WM_GetDrawPos(w, &wx, &wy, &ww, &wh);
    
    int cx = wx + 8;
    int cy = wy + 30;
    
    int maxTextWidth = ww - 16;  /* Content width with margins */
    
    DrawTextClipped(fb, cx, cy, "$ system --version", COLOR_TEXT, maxTextWidth);
    cy += 18;
    DrawTextClipped(fb, cx, cy, "  RTOS v1.0.0", COLOR_GRAY, maxTextWidth);
    cy += 18;
    DrawTextClipped(fb, cx, cy, "$ uptime", COLOR_TEXT, maxTextWidth);
    cy += 18;
    DrawTextClipped(fb, cx, cy, "  0d 2:34:12", COLOR_GRAY, maxTextWidth);
    cy += 22;
    DrawText(fb, cx, cy, "$ _", COLOR_TEXT);
}

static void WM_DrawFilesContent(uint32_t *fb, window_t *w)
{
    if (!w->is_open || w->is_minimized) return;
    
    int16_t wx, wy, ww, wh;
    WM_GetDrawPos(w, &wx, &wy, &ww, &wh);
    
    int cx = wx + 8;
    int cy = wy + 30;
    
    DrawText(fb, cx, cy, "/home/user", COLOR_BLUE);
    cy += 20;
    DrawText(fb, cx, cy, "[D] Documents", COLOR_TEXT);
    cy += 18;
    DrawText(fb, cx, cy, "[D] Downloads", COLOR_TEXT);
    cy += 18;
    DrawText(fb, cx, cy, "[F] readme.txt", COLOR_TEXT);
    cy += 18;
    DrawText(fb, cx, cy, "[F] config.ini", COLOR_TEXT);
}

static void WM_DrawMonitorContent(uint32_t *fb, window_t *w)
{
    if (!w->is_open || w->is_minimized) return;
    
    int16_t wx, wy, ww, wh;
    WM_GetDrawPos(w, &wx, &wy, &ww, &wh);
    
    int cx = wx + 8;
    int cy = wy + 30;
    char buf[32];
    
    sprintf(buf, "CPU: %.0f%%", s_wm.cpu_usage);
    DrawText(fb, cx, cy, buf, COLOR_TEXT);
    cy += 20;
    
    /* CPU graph with rounded corners */
    DrawRoundRect(fb, cx, cy, 160, 40, BTN_CORNER_RADIUS, COLOR_WHITE, true);
    DrawRoundRect(fb, cx, cy, 160, 40, BTN_CORNER_RADIUS, COLOR_GRAY, false);
    for (int i = 0; i < 20; i++) {
        int idx = (s_wm.history_idx + i) % 20;
        int barH = (int)(s_wm.cpu_history[idx] * 36 / 100);
        if (barH > 0) {
            DrawRoundRect(fb, cx + 2 + i * 8, cy + 38 - barH, 6, barH, 2, COLOR_BLUE, true);
        }
    }
    cy += 48;
    
    sprintf(buf, "Memory: %.0f%%", s_wm.mem_usage);
    DrawText(fb, cx, cy, buf, COLOR_TEXT);
    cy += 20;
    /* Memory bar with rounded corners */
    DrawRoundRect(fb, cx, cy, 100, 10, 4, COLOR_GRAY, false);
    int memWidth = (int)(s_wm.mem_usage * 94 / 100);
    if (memWidth > 4) {
        DrawRoundRect(fb, cx + 3, cy + 2, memWidth, 6, 2, COLOR_RED, true);
    }
}

static void WM_DrawAboutContent(uint32_t *fb, window_t *w)
{
    if (!w->is_open || w->is_minimized) return;
    
    int16_t wx, wy, ww, wh;
    WM_GetDrawPos(w, &wx, &wy, &ww, &wh);
    
    int cx = wx + 4;
    int cy = wy + 32;
    int contentWidth = ww - 8;
    
    DrawTextCentered(fb, cx, cy, contentWidth, "RTOS Desktop", COLOR_PURPLE);
    cy += 22;
    DrawTextCentered(fb, cx, cy, contentWidth, "Version 1.0.0", COLOR_TEXT);
    cy += 20;
    DrawTextCentered(fb, cx, cy, contentWidth, "i.MX RT1052", COLOR_TEXT);
    cy += 20;
    DrawTextCentered(fb, cx, cy, contentWidth, "emGUI", COLOR_BLUE);
    cy += 26;
    
    /* OK button - centered with rounded corners */
    int btnW = 60;
    int btnH = 24;
    int btnX = cx + (contentWidth - btnW) / 2;
    DrawRoundRect(fb, btnX, cy, btnW, btnH, BTN_CORNER_RADIUS, COLOR_BUTTON, true);
    DrawRoundRect(fb, btnX, cy, btnW, btnH, BTN_CORNER_RADIUS, COLOR_GRAY, false);
    DrawTextCentered(fb, btnX, cy + 3, btnW, "OK", COLOR_TEXT);
}

/*******************************************************************************
 * Drawing - Desktop, Taskbar, Menu
 ******************************************************************************/
static void WM_DrawDesktop(uint32_t *fb)
{
    /* Check if a maximized window covers the entire desktop - skip drawing */
    for (int i = 1; i < WND_COUNT; i++) {
        window_t *w = &s_wm.windows[i];
        if (w->is_open && !w->is_minimized && w->is_maximized && !w->animating) {
            /* Maximized window covers desktop completely, skip desktop fill */
            return;
        }
    }
    
    /* Fill entire desktop */
    DrawRect(fb, 0, 0, WM_SCREEN_W, WM_SCREEN_H - WM_TASKBAR_HEIGHT, COLOR_DESKTOP, true);
}

static void WM_DrawTaskbar(uint32_t *fb)
{
    int y = WM_SCREEN_H - WM_TASKBAR_HEIGHT;
    char buf[32];
    
    /* Modern taskbar with gradient effect */
    /* Base layer - dark */
    DrawRect(fb, 0, y, WM_SCREEN_W, WM_TASKBAR_HEIGHT, 0xFF1A1A2E, true);
    /* Top highlight - subtle glass effect */
    DrawRectAlpha(fb, 0, y, WM_SCREEN_W, 1, 0xFFFFFFFF, 40);
    /* Gradient bands for depth */
    DrawRectAlpha(fb, 0, y + 1, WM_SCREEN_W, 6, 0xFFFFFFFF, 20);
    DrawRectAlpha(fb, 0, y + 7, WM_SCREEN_W, 4, 0xFFFFFFFF, 10);
    /* Bottom shadow line */
    DrawRectAlpha(fb, 0, y + WM_TASKBAR_HEIGHT - 1, WM_SCREEN_W, 1, 0xFF000000, 60);
    
    /* Start button with glass effect */
    uint32_t startColor = s_wm.start_menu_open ? COLOR_PURPLE : 0xFF2D2D44;
    uint8_t startAlpha = s_wm.start_menu_open ? 255 : 220;
    DrawRoundRectAlpha(fb, 2, y + 2, 56, 24, BTN_CORNER_RADIUS, startColor, startAlpha);
    /* Highlight on top of button */
    DrawRectAlpha(fb, 4, y + 3, 52, 6, 0xFFFFFFFF, 30);
    DrawRoundRect(fb, 2, y + 2, 56, 24, BTN_CORNER_RADIUS, 0xFF404060, false);
    DrawText(fb, 10, y + 5, "Start", s_wm.start_menu_open ? COLOR_WHITE : 0xFFE0E0E0);
    
    /* Window buttons with glass effect */
    int btnX = 65;
    for (int i = 1; i < WND_COUNT; i++) {
        if (s_wm.windows[i].is_open) {
            bool isActive = (s_wm.active_window == i && !s_wm.windows[i].is_minimized);
            uint32_t btnColor = isActive ? COLOR_PURPLE : 0xFF2D2D44;
            uint8_t btnAlpha = isActive ? 240 : 180;
            uint32_t txtColor = isActive ? COLOR_WHITE : 0xFFD0D0D0;
            
            DrawRoundRectAlpha(fb, btnX, y + 2, 55, 24, BTN_CORNER_RADIUS, btnColor, btnAlpha);
            /* Highlight on top of button */
            DrawRectAlpha(fb, btnX + 2, y + 3, 51, 5, 0xFFFFFFFF, isActive ? 40 : 25);
            DrawRoundRect(fb, btnX, y + 2, 55, 24, BTN_CORNER_RADIUS, isActive ? 0xFF6060A0 : 0xFF404060, false);
            DrawTextClipped(fb, btnX + 4, y + 5, s_wm.windows[i].title, txtColor, 47);
            btnX += 58;
        }
    }
    
    /* Right side - FPS and Clock with subtle background */
    DrawRoundRectAlpha(fb, WM_SCREEN_W - 140, y + 2, 136, 24, BTN_CORNER_RADIUS, 0xFF000000, 60);
    sprintf(buf, "%uFPS", (unsigned int)s_wm.fps);
    DrawText(fb, WM_SCREEN_W - 130, y + 5, buf, 0xFF80FF80);  /* Green FPS */
    
    sprintf(buf, "%02d:%02d", s_wm.hour, s_wm.minute);
    DrawText(fb, WM_SCREEN_W - 48, y + 5, buf, 0xFFE0E0E0);
}

static void WM_DrawStartMenu(uint32_t *fb)
{
    if (!s_wm.start_menu_open) return;
    
    int x = 0;
    int y = WM_SCREEN_H - WM_TASKBAR_HEIGHT - 160;
    int w = 130;
    int h = 160;
    
    /* Menu background with rounded corners (left side only since it's at edge) */
    DrawRoundRect(fb, x, y, w, h, WND_CORNER_RADIUS, COLOR_WINDOW_BG, true);
    DrawRoundRect(fb, x, y, w, h, WND_CORNER_RADIUS, COLOR_PURPLE, false);
    
    /* Menu items with rounded hover highlight */
    const char *items[] = {"# Settings", "> Terminal", "= Files", "@ Monitor", "? About"};
    int iy = y + 6;
    for (int i = 0; i < 5; i++) {
        /* Highlight if mouse over */
        if (PointInRect(s_input.x, s_input.y, x, iy, w, 28)) {
            DrawRoundRect(fb, x + 2, iy + 2, w - 4, 26, BTN_CORNER_RADIUS, COLOR_BUTTON_HOVER, true);
            DrawText(fb, x + 10, iy + 4, items[i], COLOR_WHITE);
        } else {
            DrawText(fb, x + 10, iy + 4, items[i], COLOR_TEXT);
        }
        iy += 30;
    }
}

static void WM_DrawCursor(uint32_t *fb, int x, int y)
{
    uint32_t color = s_input.pressed ? COLOR_RED : COLOR_PURPLE;
    
    /* Simple arrow cursor - use lines instead of individual pixels */
    /* Vertical line */
    DrawRect(fb, x, y, 1, 12, color, true);
    /* Diagonal line - use small rects for speed */
    for (int i = 0; i < 8; i++) {
        DrawPixel(fb, x + i, y + i, color);
    }
    /* Arrow bottom part */
    DrawRect(fb, x + 1, y + 9, 1, 3, color, true);
    DrawRect(fb, x + 4, y + 9, 2, 2, color, true);
}

/* Check if window A is fully covered by window B */
static bool WM_IsWindowOccluded(window_t *a, window_t *b)
{
    if (!b->is_open || b->is_minimized) return false;
    
    int16_t ax = a->animating ? (int16_t)a->anim_x : a->x;
    int16_t ay = a->animating ? (int16_t)a->anim_y : a->y;
    int16_t aw = a->animating ? (int16_t)a->anim_w : a->w;
    int16_t ah = a->animating ? (int16_t)a->anim_h : a->h;
    
    int16_t bx = b->animating ? (int16_t)b->anim_x : b->x;
    int16_t by = b->animating ? (int16_t)b->anim_y : b->y;
    int16_t bw = b->animating ? (int16_t)b->anim_w : b->w;
    int16_t bh = b->animating ? (int16_t)b->anim_h : b->h;
    
    /* A is fully inside B? */
    return (ax >= bx && ay >= by && ax + aw <= bx + bw && ay + ah <= by + bh);
}

/*******************************************************************************
 * Main Draw Function
 ******************************************************************************/
static void WM_DrawAll(uint32_t *fb)
{
    uint32_t t0, t1;
    
    /* Desktop */
    t0 = DWT_GetCycles();
    WM_DrawDesktop(fb);
    t1 = DWT_GetCycles();
    Profile_Update(&s_profiler.draw_desktop, t0, t1);
    
    /* Windows (draw all, active last) */
    t0 = DWT_GetCycles();
    window_t *active = (s_wm.active_window > 0) ? &s_wm.windows[s_wm.active_window] : NULL;
    
    for (int i = 1; i < WND_COUNT; i++) {
        if (i != s_wm.active_window) {
            window_t *w = &s_wm.windows[i];
            
            /* Skip if fully occluded by active window */
            if (active && WM_IsWindowOccluded(w, active)) continue;
            
            WM_DrawWindow(fb, w);
            if (i == WND_SETTINGS) WM_DrawSettingsContent(fb, w);
            else if (i == WND_TERMINAL) WM_DrawTerminalContent(fb, w);
            else if (i == WND_FILES) WM_DrawFilesContent(fb, w);
            else if (i == WND_MONITOR) WM_DrawMonitorContent(fb, w);
            else if (i == WND_ABOUT) WM_DrawAboutContent(fb, w);
        }
    }
    /* Active window on top */
    if (s_wm.active_window > 0) {
        WM_DrawWindow(fb, &s_wm.windows[s_wm.active_window]);
        if (s_wm.active_window == WND_SETTINGS) WM_DrawSettingsContent(fb, &s_wm.windows[s_wm.active_window]);
        else if (s_wm.active_window == WND_TERMINAL) WM_DrawTerminalContent(fb, &s_wm.windows[s_wm.active_window]);
        else if (s_wm.active_window == WND_FILES) WM_DrawFilesContent(fb, &s_wm.windows[s_wm.active_window]);
        else if (s_wm.active_window == WND_MONITOR) WM_DrawMonitorContent(fb, &s_wm.windows[s_wm.active_window]);
        else if (s_wm.active_window == WND_ABOUT) WM_DrawAboutContent(fb, &s_wm.windows[s_wm.active_window]);
    }
    t1 = DWT_GetCycles();
    Profile_Update(&s_profiler.draw_windows, t0, t1);
    
    /* Start Menu (on top of windows) */
    t0 = DWT_GetCycles();
    WM_DrawStartMenu(fb);
    t1 = DWT_GetCycles();
    Profile_Update(&s_profiler.draw_startmenu, t0, t1);
    
    /* Taskbar (always on top) */
    t0 = DWT_GetCycles();
    WM_DrawTaskbar(fb);
    t1 = DWT_GetCycles();
    Profile_Update(&s_profiler.draw_taskbar, t0, t1);
    
    /* Cursor */
    t0 = DWT_GetCycles();
    WM_DrawCursor(fb, s_input.x, s_input.y);
    t1 = DWT_GetCycles();
    Profile_Update(&s_profiler.draw_cursor, t0, t1);
}

/* SysTick_Handler is provided by CRTOS - do not define here */

/* SysTick is initialized by CRTOS - do not initialize here */
#if 0
static void SysTick_Init(void)
{
    SysTick_Config(SystemCoreClock / 1000U);
}
#endif

/*******************************************************************************
 * User Mode Test Tasks
 * NOTE: USER mode tasks cannot access peripherals (UART, etc.) directly!
 *       They must use syscalls or shared memory for communication.
 ******************************************************************************/

/* Test task handles */
static CRTOS::Task::TaskHandle s_userTestTaskHandle = nullptr;
static CRTOS::Task::TaskHandle s_privilegeDropTaskHandle = nullptr;

/* Shared status variables for USER mode tasks (privileged task can read/print these) */
static volatile uint32_t s_userTestCounter = 0;
static volatile uint32_t s_userTestStarted = 0;
static volatile uint32_t s_privDropStarted = 0;
static volatile uint32_t s_privDropDropped = 0;
static volatile uint32_t s_privDropCounter = 0;
static volatile uint32_t s_futexProducerCounter = 0;
static volatile uint32_t s_futexConsumerCounter = 0;
static volatile uint32_t s_userTestElevateCount = 0;

/*
 * User Mode Test Task 1: Simple unprivileged task with privilege elevation
 * This task runs in USER mode but periodically elevates to PRIVILEGED to use PRINTF
 */
static void UserModeTestTask(void *args)
{
    (void)args;
    
    s_userTestStarted = 1;
    
    /* User mode task can do normal computation */
    while (1)
    {
        s_userTestCounter++;
        
        /* Every 1000 iterations (about 10 seconds), elevate and print */
        if ((s_userTestCounter % 1000) == 0)
        {
            s_userTestElevateCount++;
            
            /* Elevate to privileged mode */
            CRTOS::Task::ElevatePrivileges();
            
            /* Now we're privileged - can use PRINTF! */
            PRINTF("[UserTest] Elevated! counter=%lu, elevations=%lu\r\n", 
                   s_userTestCounter, s_userTestElevateCount);
            
            /* Drop back to user mode */
            CRTOS::Task::DropPrivileges();
            
            /* Now we're back in user mode - NO PRINTF! */
        }
        
        /* Yield to other tasks */
        CRTOS::Task::Delay(10);
    }
}

/*
 * User Mode Test Task 2: Privilege drop test
 * Starts PRIVILEGED (can use PRINTF), then drops to USER mode.
 * After dropping, NO MORE PRINTF - uses shared variables.
 */
static void PrivilegeDropTestTask(void *args)
{
    (void)args;
    
    /* While still privileged, we can use PRINTF */
    PRINTF("[PrivDropTest] Task started in PRIVILEGED mode\r\n");
    
    /* Verify we're privileged initially */
    bool isPriv = CRTOS::Task::IsCurrentTaskPrivileged();
    PRINTF("[PrivDropTest] Initial privilege: %s\r\n", isPriv ? "PRIVILEGED" : "USER");
    
    /* Do some privileged operations */
    PRINTF("[PrivDropTest] Performing privileged operations...\r\n");
    s_privDropStarted = 1;
    
    /* Now drop privileges - THIS IS ONE-WAY! */
    PRINTF("[PrivDropTest] Dropping privileges NOW...\r\n");
    
    /* Use inline syscall to drop privileges */
    register int32_t r0 __asm__("r0");
    __asm__ volatile (
        "svc %[svc_num]"
        : "=r" (r0)
        : [svc_num] "I" (104)  /* SYS_DROP_PRIVILEGES */
        : "memory"
    );
    
    /* NOW WE ARE IN USER MODE - NO MORE PRINTF! */
    s_privDropDropped = 1;
    
    /* Continue running in user mode */
    while (1)
    {
        s_privDropCounter++;
        CRTOS::Task::Delay(10);
    }
}

/*
 * User Mode Test Task 3: Futex-based synchronization test
 * Two tasks using futex for synchronization.
 * NOTE: These run in USER mode - NO PRINTF allowed!
 */
static volatile int32_t s_futexTestValue = 0;

static void FutexProducerTask(void *args)
{
    (void)args;
    
    /* USER mode - no PRINTF! Use shared counter instead */
    
    while (1)
    {
        /* Simulate producing data */
        CRTOS::Task::Delay(1000);
        
        /* Update shared value and wake waiter */
        __sync_fetch_and_add(&s_futexTestValue, 1);
        s_futexProducerCounter++;
        
        /* Wake one waiter on the futex */
        CRTOS::Futex::Wake((uint32_t*)&s_futexTestValue, 1);
    }
}

static void FutexConsumerTask(void *args)
{
    (void)args;
    
    /* USER mode - no PRINTF! Use shared counter instead */
    int32_t lastValue = 0;
    
    while (1)
    {
        /* Wait for value to change */
        int32_t currentValue = s_futexTestValue;
        
        if (currentValue == lastValue)
        {
            /* Wait on futex - will be woken by producer */
            CRTOS::Futex::Wait((uint32_t*)&s_futexTestValue, currentValue, 5000);
        }
        
        /* Check if value changed */
        if (s_futexTestValue != lastValue)
        {
            s_futexConsumerCounter++;
            lastValue = s_futexTestValue;
        }
    }
}

/*******************************************************************************
 * UART3 Echo Task - Echoes received characters back
 ******************************************************************************/
static CRTOS::Task::TaskHandle s_uart3EchoTaskHandle;
static CRTOS::Drivers::DriverBase* s_uart3Driver = nullptr;

static void UART3EchoTask(void* args)
{
    (void)args;
    
    while (s_uart3Driver == nullptr)
    {
        CRTOS::Task::Delay(10);
    }
    
    PRINTF("UART3EchoTask: Driver ready, starting echo loop\r\n");
    
    char buffer[64];
    
    while (1)
    {
        // Blocking read with 100ms timeout - waits on semaphore instead of polling
        int32_t bytesRead = CALL_DRIVER_METHOD(s_uart3Driver, read, buffer, sizeof(buffer) - 1, 100);
        
        if (bytesRead > 0)
        {
            // Echo back the received data immediately
            CALL_DRIVER_METHOD(s_uart3Driver, write, buffer, bytesRead, 0);
        }
    }
}

/*******************************************************************************
 * Driver Module Loader Task - Loads drivers from SD card at startup
 ******************************************************************************/
static CRTOS::Task::TaskHandle s_driverLoaderTaskHandle;

static void DriverLoaderTask(void* args)
{
    (void)args;
    
    /* Initialize SD card */
    CRTOS::HAL::SDCard& sd = CRTOS::HAL::GetSDCard();
    CRTOS::Result result = sd.Initialize();
    
    if (result != CRTOS::Result::RESULT_SUCCESS || !sd.IsCardInserted())
    {
        CRTOS::Task::Delete();
        return;
    }
    
    /* Mount filesystem */
    CRTOS::HAL::FileSystem& fs = CRTOS::HAL::GetFileSystem();
    result = fs.Mount("");
    
    if (result != CRTOS::Result::RESULT_SUCCESS)
    {
        CRTOS::Task::Delete();
        return;
    }
    
    /* Initialize driver loader and load all modules from /drivers */
    CRTOS::InitDriverLoader();
    CRTOS::LoadDriverModules("/drivers");
    
    /* ============================================================
     * UART3 Driver Initialization (using GPIO_AD_B1_06/07 pins)
     * ============================================================ */
    
    // First, configure pins using PinMux driver
    CRTOS::Drivers::DriverBase* pinmuxDriver = CRTOS::Drivers::getDriver("pinmux");

    if (pinmuxDriver != nullptr)
    {
        // Configure GPIO_AD_B1_06 as LPUART3_TXD (ALT2)
        CRTOS::Drivers::PinMuxConfig txConfig;
        strncpy(txConfig.pinName, "GPIO_AD_B1_06", sizeof(txConfig.pinName));
        txConfig.altFunction = 2;  // ALT2 = LPUART3_TXD
        txConfig.sion = false;
        
        CALL_DRIVER_METHOD(pinmuxDriver, ioctl,
            static_cast<uint32_t>(CRTOS::Drivers::PinMuxIoctl::SET_PIN_MUX), &txConfig);
        
        // Configure GPIO_AD_B1_07 as LPUART3_RXD (ALT2)
        CRTOS::Drivers::PinMuxConfig rxConfig;
        strncpy(rxConfig.pinName, "GPIO_AD_B1_07", sizeof(rxConfig.pinName));
        rxConfig.altFunction = 2;  // ALT2 = LPUART3_RXD
        rxConfig.sion = false;
        
        CALL_DRIVER_METHOD(pinmuxDriver, ioctl,
            static_cast<uint32_t>(CRTOS::Drivers::PinMuxIoctl::SET_PIN_MUX), &rxConfig);
    }
    
    // Find the UART driver by name
    CRTOS::Drivers::DriverBase* uartDriver = CRTOS::Drivers::getDriver("uart");
    
    if (uartDriver != nullptr)
    {
        // Set UART instance to LPUART3 (HAL_UART_3 = 2) BEFORE opening
        uint32_t uartInstance = 2;  // HAL_UART_3
        CALL_DRIVER_METHOD(uartDriver, ioctl,
            static_cast<uint32_t>(0x0100 + 13), &uartInstance);  // SET_INSTANCE = IOCTL_BASE_UART + 13
        
        // Open the driver
        CRTOS::Drivers::DriverResult openResult = CALL_DRIVER_METHOD(uartDriver, open);
        if (openResult == CRTOS::Drivers::DriverResult::SUCCESS)
        {
            // Write welcome message to UART3
            const char* testMsg = "UART3 Echo Ready - type something!\r\n";
            CALL_DRIVER_METHOD(uartDriver, write, testMsg, strlen(testMsg), 0);
            
            // Store driver for echo task
            s_uart3Driver = uartDriver;
        }
    }
    
    /* ============================================================
     * Display Driver Initialization
     * ============================================================ */
    
    // Find the Display driver by name
    CRTOS::Drivers::DriverBase* displayDriver = CRTOS::Drivers::getDriver("display");
    
    if (displayDriver != nullptr)
    {
        // Open the driver
        CRTOS::Drivers::DriverResult openResult = CALL_DRIVER_METHOD(displayDriver, open);
        if (openResult == CRTOS::Drivers::DriverResult::SUCCESS)
        {
            // Get display dimensions via ioctl
            uint16_t width = 0, height = 0;
            CALL_DRIVER_METHOD(displayDriver, ioctl, 
                static_cast<uint32_t>(CRTOS::Drivers::DisplayIoctl::GET_WIDTH), &width);
            CALL_DRIVER_METHOD(displayDriver, ioctl, 
                static_cast<uint32_t>(CRTOS::Drivers::DisplayIoctl::GET_HEIGHT), &height);
            
            // Get framebuffer
            uint32_t* fb = nullptr;
            CALL_DRIVER_METHOD(displayDriver, ioctl, 
                static_cast<uint32_t>(CRTOS::Drivers::DisplayIoctl::GET_FRAMEBUFFER), &fb);
            
            if (fb != nullptr)
            {
                /* Initialize TFTLIB with framebuffer from driver */
                tft.setFramebuffer(fb, width, height);
                tft.setFreeFont(&FreeSans9pt7b);
                tft.setTextSize(1);
                tft.setTextDatum(TL_DATUM);
                tft.setTextColor(COLOR_TEXT, COLOR_TEXT);
            }
            
            /* Enable display output via driver */
            CALL_DRIVER_METHOD(displayDriver, ioctl, 
                static_cast<uint32_t>(CRTOS::Drivers::DisplayIoctl::ENABLE), nullptr);
            
            // Store driver for GUI task to use
            s_displayDriver = displayDriver;
        }
    }

    /* ============================================================
     * User Application Loading
     * ============================================================ */
    
    // Initialize AppLoader
    CRTOS::AppLoader& appLoader = CRTOS::AppLoader::getInstance();
    CRTOS::AppResult appResult = appLoader.init();
    
    if (appResult == CRTOS::AppResult::SUCCESS)
    {
        PRINTF("AppLoader: Initialized successfully\r\n");
        
        // Try to load and run hello.app from /apps directory
        // Priority 5 (higher), stack 8192 bytes (larger for printf buffer)
        appResult = appLoader.loadAndRunApp("/apps/hello.app", nullptr, 5, 8192);
        
        if (appResult == CRTOS::AppResult::SUCCESS)
        {
            PRINTF("AppLoader: hello.app loaded and started\r\n");
        }
        else
        {
            PRINTF("AppLoader: Failed to load hello.app (error=%d)\r\n", (int)appResult);
        }
        
        // List loaded applications
        appLoader.listApps();
    }
    else
    {
        PRINTF("AppLoader: Failed to initialize (error=%d)\r\n", (int)appResult);
    }

    /* Task complete - delete self */
    CRTOS::Task::Delete();
}

/* Create user mode test tasks */
static void CreateUserModeTestTasks(void)
{
    CRTOS::Task::TaskHandle handle;
    
    /* Test 1: Simple user mode task */
    CRTOS::Task::Create(
        UserModeTestTask,
        "UserTest",
        1024,
        nullptr,
        3,
        &s_userTestTaskHandle,
        CRTOS::Task::PrivilegeMode::USER
    );
    
    /* Test 2: Privilege drop task */
    CRTOS::Task::Create(
        PrivilegeDropTestTask,
        "PrivDrop",
        1024,
        nullptr,
        3,
        &s_privilegeDropTaskHandle,
        CRTOS::Task::PrivilegeMode::PRIVILEGED
    );
    
    /* Test 3: Futex producer (user mode) */
    CRTOS::Task::Create(
        FutexProducerTask,
        "FutexProd",
        1024,
        nullptr,
        3,
        &handle,
        CRTOS::Task::PrivilegeMode::USER
    );
    
    /* Test 4: Futex consumer (user mode) */
    CRTOS::Task::Create(
        FutexConsumerTask,
        "FutexCons",
        1024,
        nullptr,
        3,
        &handle,
        CRTOS::Task::PrivilegeMode::USER
    );
}

/*******************************************************************************
 * GUI Task - Runs the Window Manager rendering loop
 ******************************************************************************/
static void GUI_Task(void *args)
{
    (void)args;
    
    /* Wait for display driver to be loaded and initialized by DriverLoaderTask */
    uint32_t waitStart = tickCount;
    const uint32_t DRIVER_WAIT_TIMEOUT_MS = 10000;
    
    while (s_displayDriver == nullptr)
    {
        if ((tickCount - waitStart) > DRIVER_WAIT_TIMEOUT_MS)
        {
            while (1) { CRTOS::Task::Delay(1000); }
        }
        CRTOS::Task::Delay(10);
    }
    
    // ===== Boot Manager - Load Modules from SD Card =====
    // SD card initialization is now handled by BootManager::InitializeStorage
    PRINTF("\r\n");
    CRTOS::BootManager &bootMgr = CRTOS::GetBootManager();

    // Configure boot manager
    CRTOS::BootConfig bootConfig;
    bootConfig.bootDirectory = "0:/boot";       // Load .bin files from /boot
    bootConfig.configFile = "0:/boot/boot.cfg"; // Optional priority config
    bootConfig.bootLogoFile = "0:/boot/logo.bmp"; // Boot logo (BMP format)
    bootConfig.bootLogoDisplayMs = 2000;        // Display logo for 2 seconds
    bootConfig.defaultPriority = 5;             // Default task priority
    bootConfig.sdCardTimeoutMs = 5000;          // Wait up to 5 seconds for SD card
    bootConfig.waitForCard = true;              // Wait for SD card
    bootConfig.verboseOutput = true;            // Show loading details
    bootConfig.maxModules = 16;                 // Max modules to load

    // Boot from SD card (will initialize SD card, mount filesystem, and load modules)
    bootMgr.DisplayBootLogo(bootConfig.bootLogoFile, bootConfig.bootLogoDisplayMs);

    // ===== Display All Loaded Modules =====
    PRINTF("\r\n--- Loaded Modules Summary ---\r\n");
    PRINTF("Total modules: %lu\r\n", CRTOS::Task::LPC55S69_Features::GetLoadedModulesCount());

    CRTOS::ModuleInfo modules[16];
    uint32_t count = CRTOS::Task::LPC55S69_Features::GetAllModulesInfo(modules, 16);

    for (uint32_t i = 0; i < count; i++)
    {
        PRINTF("  [%lu] %s - %lu bytes at 0x%08lX\r\n",
               i, modules[i].name, modules[i].totalSize, modules[i].baseAddress);
    }

    while (1)
    {
        uint32_t frame_start = DWT_GetCycles();
        uint32_t t0, t1;
        
        /* Update state */
        t0 = DWT_GetCycles();
        WM_Update();
        t1 = DWT_GetCycles();
        Profile_Update(&s_profiler.wm_update, t0, t1);
        
        /* Update window animations */
        t0 = DWT_GetCycles();
        WM_UpdateAnimations();
        t1 = DWT_GetCycles();
        Profile_Update(&s_profiler.wm_animations, t0, t1);
        
        /* Simulate input */
        t0 = DWT_GetCycles();
        WM_SimulateInput(&s_input);
        t1 = DWT_GetCycles();
        Profile_Update(&s_profiler.wm_simulate, t0, t1);
        
        /* Process input */
        t0 = DWT_GetCycles();
        WM_ProcessInput(&s_input);
        t1 = DWT_GetCycles();
        Profile_Update(&s_profiler.wm_process, t0, t1);
        
        /* Detect changes and build dirty rectangle list */
        Dirty_DetectChanges();
        
        /* Get the back buffer - from driver or HAL */
        uint32_t* backBuffer = nullptr;
        if (s_displayDriver != nullptr)
        {
            CALL_DRIVER_METHOD(s_displayDriver, ioctl, 
                static_cast<uint32_t>(CRTOS::Drivers::DisplayIoctl::GET_FRAMEBUFFER), 
                &backBuffer);
        }
        else
        {
            backBuffer = CRTOS::HAL::GlobalDisplay.GetDrawBuffer();
        }
        tft.setFramebuffer(backBuffer, WM_SCREEN_W, WM_SCREEN_H);
        
        /* Draw to back buffer */
        t0 = DWT_GetCycles();
        WM_DrawAll(backBuffer);
        t1 = DWT_GetCycles();
        Profile_Update(&s_profiler.wm_draw, t0, t1);
        
        /* Save state for next frame and clear dirty list */
        Dirty_SaveState();
        Dirty_Clear();

        /* Swap buffers via display driver */
        CALL_DRIVER_METHOD(s_displayDriver, ioctl, 
            static_cast<uint32_t>(CRTOS::Drivers::DisplayIoctl::SWAP_BUFFERS), 
            nullptr);

        /* Wait for VSync via display driver */
        t0 = DWT_GetCycles();
        {
            CRTOS::Drivers::DisplayVSyncArg vsyncArg = { .timeout_ms = 0xFFFFFFFF, .success = false };
            CALL_DRIVER_METHOD(s_displayDriver, ioctl, 
                static_cast<uint32_t>(CRTOS::Drivers::DisplayIoctl::WAIT_VSYNC), 
                &vsyncArg);
        }
        t1 = DWT_GetCycles();
        Profile_Update(&s_profiler.vsync_wait, t0, t1);
        
        /* Total frame time */
        Profile_Update(&s_profiler.frame_total, frame_start, DWT_GetCycles());
    }
}

/*******************************************************************************
 * Main
 ******************************************************************************/
int main(void)
{
    /* Initialize clock driver variables that are zeroed when app starts from bootloader. */
    CLOCK_SetXtalFreq(24000000U);
    CLOCK_SetRtcXtalFreq(32768U);

    BOARD_InitDebugConsole();

    CRTOS::Result result;

    // ========================================================================
    // Memory Region Setup
    // ========================================================================
    void* sramHeapStart = (void*)__end_bss_SRAM_ITC;
    uint32_t sramHeapSize = (uint32_t)__top_SRAM_ITC - (uint32_t)__end_bss_SRAM_ITC;
    
    HeapAllocator::RegisterRegion(
        "KERNEL", sramHeapStart, sramHeapSize,
        Memory::MEM_FAST | Memory::MEM_CACHED
    );
    
    void* ocramHeapStart = (void*)__end_bss_SRAM_OC;
    uint32_t ocramHeapSize = (uint32_t)__top_SRAM_OC - (uint32_t)__end_bss_SRAM_OC;
    
    HeapAllocator::RegisterRegion(
        "OCRAM", ocramHeapStart, ocramHeapSize,
        Memory::MEM_FAST | Memory::MEM_CACHED | Memory::MEM_DMA_CAPABLE
    );
    
    void* sdramHeapStart = (void*)__end_bss_BOARD_SDRAM;
    uint32_t sdramHeapSize = (uint32_t)__top_BOARD_SDRAM - (uint32_t)__end_bss_BOARD_SDRAM;
    
    HeapAllocator::RegisterRegion(
        "SDRAM", sdramHeapStart, sdramHeapSize,
        Memory::MEM_LARGE | Memory::MEM_DMA_CAPABLE
    );
    
    void* ncacheHeapStart = (void*)__end_bss_NCACHE_REGION;
    uint32_t ncacheHeapSize = (uint32_t)__top_NCACHE_REGION - (uint32_t)__end_bss_NCACHE_REGION;
    
    HeapAllocator::RegisterRegion(
        "SDRAM_NC", ncacheHeapStart, ncacheHeapSize,
        Memory::MEM_LARGE | Memory::MEM_DMA_CAPABLE | Memory::MEM_NON_CACHED
    );

    // Initialize Heap Tracker for memory leak detection
    CRTOS::HeapTracker::Init();
    PRINTF("[INIT] HeapTracker initialized\r\n");

    CRTOS::Config::SetCoreClock(600000000u);
    CRTOS::Config::SetTickRate(1000u);
    CRTOS::Config::SetTimeSlice(1u);

    CRTOS::Task::SetStackOverflowHook(myStackOverflowHook);
    
    /* Initialize IRQ Dispatcher BEFORE Display */
    CRTOS_IRQDispatcher_Init();
    
    /* Initialize and start DPC Worker for interrupt bottom-half processing */
    result = CRTOS::DPC::Init();
    if (result != CRTOS::Result::RESULT_SUCCESS)
    {
        PRINTF("DPC::Init() failed!\r\n");
    }
    result = CRTOS::DPC::Start();
    if (result != CRTOS::Result::RESULT_SUCCESS)
    {
        PRINTF("DPC::Start() failed!\r\n");
    }
    
    /* Initialize Window Manager */
    WM_Init();
    
    /* Initialize DWT cycle counter for profiling */
    DWT_Init();

    /* Create driver loader task (higher priority, runs once at startup to load modules from SD) */
    result = CRTOS::Task::Create(
        DriverLoaderTask,
        "DrvLoad",
        4096,
        nullptr,
        4,
        &s_driverLoaderTaskHandle,
        CRTOS::Task::PrivilegeMode::PRIVILEGED
    );

    /* Create UART3 echo task */
    result = CRTOS::Task::Create(
        UART3EchoTask,
        "UART3",
        1024,
        nullptr,
        3,
        &s_uart3EchoTaskHandle,
        CRTOS::Task::PrivilegeMode::PRIVILEGED
    );

    /* Create GUI task with normal priority (privileged mode for system access) */
    result = CRTOS::Task::Create(
        GUI_Task,
        "GUI",
        4096,
        nullptr,
        3,
        &s_guiTaskHandle,
        CRTOS::Task::PrivilegeMode::PRIVILEGED
    );
    
    if (result != CRTOS::Result::RESULT_SUCCESS) {
        while (1) {}
    }
    
    /* Start CRTOS scheduler - this never returns */
    CRTOS::Scheduler::Start();
    
    while (1) {}
}
