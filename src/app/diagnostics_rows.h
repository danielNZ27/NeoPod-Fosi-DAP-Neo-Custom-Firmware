#pragma once

const char *diagnostics_row_firmware(const void *context);
const char *diagnostics_row_battery(const void *context);
const char *diagnostics_row_jack_single_ended(const void *context);
const char *diagnostics_row_jack_balanced(const void *context);
const char *diagnostics_row_start(const void *context);
const char *diagnostics_row_last_step(const void *context);
const char *diagnostics_row_last_battery(const void *context);
const char *diagnostics_row_usb_last(const void *context);

const char *diagnostics_row_headphones(const void *context);
const char *diagnostics_row_jack_raw_changes(const void *context);
const char *diagnostics_row_jack_stable_changes(const void *context);
const char *diagnostics_row_gpio0_inputs(const void *context);
const char *diagnostics_row_gpio1_inputs(const void *context);
const char *diagnostics_row_gpio2_inputs(const void *context);
const char *diagnostics_row_dac_hp_link(const void *context);
const char *diagnostics_row_dac_hp_control(const void *context);
const char *diagnostics_row_dac_hp_status(const void *context);
const char *diagnostics_row_jack_pull_bit(const void *context);
