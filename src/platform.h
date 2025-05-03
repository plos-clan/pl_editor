/**
 * platform.h - Platform abstraction interface
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include "pleditor.h"

/**
 * Platform abstraction interface
 * These functions must be implemented for each platform
 */

/* Initialize terminal settings */
bool pleditor_platform_init(void);

/* Restore terminal settings before exit */
void pleditor_platform_cleanup(void);

/* Get terminal window size */
bool pleditor_platform_get_window_size(int *rows, int *cols);

/* Read a key from the terminal (with timeout in ms, 0 = no timeout) */
int pleditor_platform_read_key(int timeout_ms);

/* Write string to terminal */
void pleditor_platform_write(const char *s, size_t len);

/* Get current time */
time_t pleditor_platform_get_time(void);

/* File operations */
bool pleditor_platform_read_file(const char *filename, char **buffer, size_t *len);
bool pleditor_platform_write_file(const char *filename, const char *buffer, size_t len);

#endif /* PLATFORM_H */