/* A pretend iPhone (or Android phone) for the simulators: it feeds phone.h like the BLE link will, and answers the keychain's commands. */
#pragma once

void fake_phone_init(void);
/* kind: 'L' link (0 off, 1 pairing, 2 on), 'A' the same for an Android phone (only the shutter works), 'N' the next sample notification, 'C' call (1 rings, 0 hangs up),
   'M' music (1 plays the sample album, 0 stops) */
void fake_phone(char kind, int arg);
const char *fake_phone_last(void); /* the last command the keychain sent, "" if none */
int fake_phone_shots(void);
void fake_phone_rssi(int dbm);     /* the link's signal the locator reads, dBm (0: none) */        /* photos taken with the shutter */
const char *fake_link_report(void); /* "link=iphone|android|off": which phone is linked */
