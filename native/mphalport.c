// SPDX-License-Identifier: GPL-3.0-or-later
#include <stddef.h>
#include "py/mphal.h"
extern void ps5_mp_console_write(const char *bytes, size_t len);
void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    ps5_mp_console_write(str, len);
}
