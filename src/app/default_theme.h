#pragma once

#include "ui/theme.h"

extern const Theme DEFAULT_THEME;
extern Theme APP_THEME;
void app_theme_apply(uint8_t color);
void app_theme_select(uint8_t theme, uint8_t color);
