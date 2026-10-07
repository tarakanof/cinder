#pragma once

#include <stdbool.h>

#define OTA_FACE_TITLE "Updating Firmware"
#define OTA_FACE_TITLE_RESTART "Restarting"
#define OTA_FACE_TITLE_W 384

/* Setters: any task. The LVGL task applies them in ota_face_frame. */
void ota_face_show(const char *version);
void ota_face_pct(int pct);
void ota_face_restarting(void);
void ota_face_hide(void);
bool ota_face_requested(void);

/* LVGL task, every frame; true while the face is the active screen. */
bool ota_face_frame(void);
