/**
 * Copyright (c) 2017 rxi, 2019 nrser
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "log.h"

static struct {
  void *udata;
  log_LockFn lock;
  FILE *fp;
  int level;
  int quiet;
} L;


static const char *level_names[] = {
  "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

static int has_init_from_env = 0;

#ifdef LOG_USE_COLOR
static const char *level_colors[] = {
  "\x1b[94m", "\x1b[36m", "\x1b[32m", "\x1b[33m", "\x1b[31m", "\x1b[35m"
};
#endif


/**
 * @brief Initialize L (logger structure) values from environment variables,
 * if present.
 * 
 * Only handles setting the log level. LOG_LEVEL_ENV_VAR is used as the env
 * var name, which defaults to "LOG_LEVEL", but a custom prefix can be added
 * by defining LOG_ENV_VAR_PREFIX.
 * 
 * Sets a flag the fist time called, then just returns immediately on subsequent
 * calls, so don't worry about calling it multiple times in multiple places.
 */
void log_init_from_env(void) {
  if (has_init_from_env) {
    return;
  }
  
  char *value = getenv(LOG_LEVEL_ENV_VAR);
  
  if (value != NULL) {
    log_set_level_from_string(value);
  }
  
  has_init_from_env = 1;
}


static void lock(void)   {
  if (L.lock) {
    L.lock(L.udata, 1);
  }
}


static void unlock(void) {
  if (L.lock) {
    L.lock(L.udata, 0);
  }
}


void log_set_udata(void *udata) {
  L.udata = udata;
}


void log_set_lock(log_LockFn fn) {
  L.lock = fn;
}


void log_set_fp(FILE *fp) {
  L.fp = fp;
}


int log_get_level() {
  return L.level;
}


const char* log_get_level_name() {
  return level_names[L.level];
}


void log_set_level(int level) {
  L.level = level;
}


/**
 * @brief Convert log level name to level integer (that you can use in in
 * log_set_level()).
 * 
 * @param name 
 *    When up-cased, should be one of level_names.
 * 
 * @return int 
 *    Log level integer, or `-1` on error (logs an error as well).
 */
int log_name_to_level(char* name) {
  char *upcased = strdup(name); // make a copy

  // adjust copy to lowercase
  unsigned char *ptr = (unsigned char *)upcased;
  while(*ptr) {
      *ptr = toupper(*ptr);
      ptr++;
  }
  
  for (int i = LOG_TRACE; i <= LOG_FATAL; i++) {
    if (strcmp(level_names[i], upcased) == 0) {
      return i;
    }
  }
  
  log_error("Level name '%s' not found", upcased);
  return -1;
}


/**
 * @brief Set level given a string, which may be the string representation of
 * one of the level integers, or one of the level_names (case insensitive).
 * 
 * @see log_set_level_by_name()
 *
 * @param string
 *    Either a string representation of a level integer - "0" (LOG_TRACE)
 *    through "5" (LOG_FATAL) - or one of the level_names - "TRACE", "DEBUG",
 *    "INFO", "WARN", "ERROR" or "FATAL" (case-insensitive).
 *
 * @return int
 *    Level that was set, or `-1` on failure.
 */
int log_set_level_from_string(char* string) {
  size_t length = strlen(string);
  
  if (length == 0) {
    log_error("Received empty string");
    return -1;
  }
  
  if (length == 1) {
    for (int level = LOG_TRACE; level <= LOG_FATAL; level++) {
      int num = string[0] - '0';
      if (num == level) {
        log_set_level(level);
        return level;
      }
    }
  }
  
  return log_set_level_by_name(string);
}

/**
 * @brief Set the log level by a level_names member.
 * 
 * @see log_name_to_level()
 * 
 * @param name
 *    One of the level_names - "TRACE", "DEBUG", "INFO", "WARN", "ERROR" or
 *    "FATAL" (case-insensitive).
 * 
 * @return int 
 *    The level that was set, or `-1` on failure.
 */
int log_set_level_by_name(char* name) {
  int level = log_name_to_level(name);
  if (level != -1) {
    log_set_level(level);
  }
  return level;
}


void log_set_quiet(int enable) {
  L.quiet = enable ? 1 : 0;
}


void log_log(int level, const char *file, int line, const char *fmt, ...) {
  if (level < L.level) {
    return;
  }

  /* Acquire lock */
  lock();

  /* Get current time */
  time_t t = time(NULL);
  struct tm *lt = localtime(&t);

  /* Log to stderr */
  if (!L.quiet) {
    va_list args;
    char buf[16];
    buf[strftime(buf, sizeof(buf), "%H:%M:%S", lt)] = '\0';
#ifdef LOG_USE_COLOR
    fprintf(
      stderr, "%s %s%-5s\x1b[0m \x1b[90m%s:%d:\x1b[0m ",
      buf, level_colors[level], level_names[level], file, line);
#else
    fprintf(stderr, "%s %-5s %s:%d: ", buf, level_names[level], file, line);
#endif
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    fflush(stderr);
  }

  /* Log to file */
  if (L.fp) {
    va_list args;
    char buf[32];
    buf[strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", lt)] = '\0';
    fprintf(L.fp, "%s %-5s %s:%d: ", buf, level_names[level], file, line);
    va_start(args, fmt);
    vfprintf(L.fp, fmt, args);
    va_end(args);
    fprintf(L.fp, "\n");
    fflush(L.fp);
  }

  /* Release lock */
  unlock();
}
