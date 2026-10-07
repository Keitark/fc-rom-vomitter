#pragma once

typedef void (*serial_status_emit_fn)(const char *line, void *context);
/* Read-only: no radio, console, SRAM, or persistent-state changes. */
void serial_status_send(serial_status_emit_fn emit, void *context);
