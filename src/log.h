/**
 * Copyright (c) 2017 rxi
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `log.c` for details.
 */

#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <stdarg.h>

// Defines the `bool` type, etc.
#include <stdbool.h>

#define LOG_VERSION "0.1.0"

#ifndef LOG_ENV_VAR_PREFIX
#define LOG_ENV_VAR_PREFIX ""
#endif

#define LOG_LEVEL_ENV_VAR (LOG_ENV_VAR_PREFIX "LOG_LEVEL")

typedef void (*log_LockFn)(void *udata, int lock);

/**
 * @brief The available levels.
 * 
 * @note @parblock
 *    Levels *may* be negative - LOG_TRACE is defined as `-1` so that the rest
 *    match with Ruby's [Logger::Severity][] constants, which would be much 
 *    harder to change, but values **MUST** be contiguous!!!
 *    
 *    If non-contiguous values are used **all sorts of stuff will break** and
 *    programs will almost certainly crash.
 *    
 *    [Logger::Severity]: https://docs.ruby-lang.org/en/2.3.0/Logger/Severity.html
 * @endparblock
 * 
 */
enum {
  LOG_TRACE = -1,
  LOG_DEBUG = 0,
  LOG_INFO = 1,
  LOG_WARN = 2,
  LOG_ERROR = 3,
  LOG_FATAL = 4
};

#define log_trace(...) log_log(LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define log_debug(...) log_log(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define log_info(...)  log_log(LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define log_warn(...)  log_log(LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define log_error(...) log_log(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define log_fatal(...) log_log(LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)


// Function Declarations (Public API)
// ===========================================================================

// Functional Utilties
// ---------------------------------------------------------------------------

bool        log_is_level              (int level);
const char *log_level_to_name         (int level);
char      **log_level_strings         (void);
char       *log_level_to_string       (int level);

#ifdef LOG_USE_COLOR

const char *log_level_to_color        (int level);

#endif // #ifdef LOG_USE_COLOR

// State
// ---------------------------------------------------------------------------

FILE       *log_get_fp                (void);
int         log_get_level             (void);
const char *log_get_level_name        (void);
bool        log_get_quiet             (void);
void        log_set_fp                (FILE *fp);
void        log_set_level             (int level);
int         log_set_level_by_name     (char* name);
int         log_set_level_from_string (char* string);
void        log_set_lock              (log_LockFn fn);
void        log_set_quiet             (bool enable);
void        log_set_udata             (void *udata);

// Doin' Stuff
// ---------------------------------------------------------------------------

void        log_init_from_env         (void);
void        log_log                   (int level,
                                       const char *file,
                                       int line,
                                       const char *fmt,
                                       ...);

#endif // #ifndef LOG_H
