/* What the board glue in main.c shares with ble.c. */
#pragma once
#include <stdbool.h>

void ui_lock(void);   /* LVGL is not thread-safe: every touch of the UI from another task goes inside */
void ui_unlock(void);
#include <stdint.h>
extern int tz_min;    /* local time zone, minutes east of UTC; the iPhone or the panel sets it, kept in NVS "tz" */
void settings_time(int64_t unix_s, int tz); /* the real clock from the panel (the computer's), and its zone */
void settings_panel(bool on); /* the panel's heartbeat ("panel 1" every ~2 s; 0, or its Bluetooth link gone, ends it): Pip shows "РЕЖИМ НАСТРОЙКИ" */
bool settings_clock_known(void); /* the real clock is set since power-on (a phone or the panel told it) */

void ble_start(void); /* the iPhone link: ANCS notifications and calls, AMS music, CTS time */
void ble_quiet(bool quiet); /* every link down and no advertising (saving energy while asleep); false: back on air */
void batlog_dump(void (*out)(const char *)); /* the battery log as "@B seq uptime_s mV % flags lit% unix_s" rows (unix_s 0: clock not set), then "@B end" */
void batlog_clear(void);
void batlog_fake(int minutes); /* debug "bat fake <min>": made-up rows ending now, to try the panel's chart */

void debug_start(void);
int board_perf(char *out, int cap); /* "@P {...}": face, LVGL and panel times since the last call, then reset (debug `perf`) */
void settings_save(void);       /* power, features, brightness, privacy into NVS */
void settings_bright(int pct);  /* the panel's full brightness, 10..100 */
int settings_brightness(void);
bool settings_auto_bright(void);
void settings_set_auto_bright(bool on); /* dimmer as the battery runs down, never darker than 20% */
void debug_line(char *line);              /* a command line from the BLE debug link (queued for the debug task) */
bool ble_debug_on(void);
bool board_usb(void);    /* on a USB cable (the debug link's USB side can be there) */
void ble_debug_send(const char *s, int n); /* to the BLE debug page */
 /* the USB debug link: status out, commands in (tools/pipdebug.py) */
