/**
 * @file src/log.c
 * @author rxi
 * @author NRSER (neil@neilsouza.com)
 * @brief A simple logging library implemented in C99 - source.
 * 
 * @copyright (c) 2017 rxi; 2019 NRSER
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

#include "log.h"

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

// Has the log10() function
#include <math.h>


// Definitions
// ===========================================================================

/**
 * @brief Error code that log_name_to_level() returns when it's given a bad
 *    level name.
 *  
 * Used to just use `-1`, but then switched the levels to trace being `-1` so
 * the rest matched Ruby's [Logger::Severity][] class constants.
 * 
 * [Logger::Severity]: https://docs.ruby-lang.org/en/2.3.0/Logger/Severity.html
 */
#define BAD_LEVEL 666


// Globals
// ===========================================================================

/**
 * @brief The logger's state (global and private).
 */
static struct {
  void *udata;
  log_LockFn lock;
  FILE *fp;
  int level;
  bool quiet;
} L;

static const char *level_names[] = {
  "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

/**
 * @brief Internally keeps track of if log_init_from_env() has been called.
 */
static int has_init_from_env = 0;

/**
 * @brief String versions of the level integers to compare input to (from 
 *        environment variables or CLI arguments).
 * 
 * Array of pointers to strings.
 * 
 * @note  Don't access directly, use log_level_strings(), which first initializes
 *        the values if needed.
 */
static char **level_strings;

#ifdef LOG_USE_COLOR
// ---------------------------------------------------------------------------

static const char *level_colors[] = {
  "\x1b[94m", "\x1b[36m", "\x1b[32m", "\x1b[33m", "\x1b[31m", "\x1b[35m"
};

#endif // #ifdef LOG_USE_COLOR ***********************************************


// Functions
// ===========================================================================

// Internal
// ---------------------------------------------------------------------------

/**
 * @brief How many characters an integer would be expressed as a string (in 
 *    decimal form).
 * 
 * @note  If allocating space remember to add `1` to account for the terminating
 *        `NULL`.
 */
static int
length_as_string(int x) {
  int length;
  
  if (0 == x) {
    // Special case for `0`, where the alg below does not work.
    length = 1;
  } else {
    length = floor(log10(abs(x))) + 1;
  }
  
  if (x < 0) {
    length++;
  }
  
  return length;
} // length_as_string()

/**
 * @brief String-ify an integer (in decimal).
 * 
 * @note  Dynamically allocates the returned string, so don't forget to free()
 *        if and when you're done.
 */
static char *
int_to_string(int x) {
  char *str = malloc((length_as_string(x) + 1) * sizeof(char));
  sprintf(str, "%d", x);
  return str;
} // int_to_string()

static void
lock(void)   {
  if (L.lock) {
    L.lock(L.udata, 1);
  }
}

static void
unlock(void) {
  if (L.lock) {
    L.lock(L.udata, 0);
  }
}


// Functional Utilties
// ---------------------------------------------------------------------------
// 
// Don't depend on no state.
// 

/**
 * @brief Is a `level` valid?
 */
bool
log_is_level(int level) {
  return level >= LOG_TRACE && level <= LOG_FATAL;
}

#ifdef LOG_USE_COLOR

/**
 * @brief Get the shell color string for a level.
 * 
 * @param level 
 * @return const char * String (`NULL`-termianted). *DO NOT* free()!
 * @return NULL When level is not valid.
 */
const char *
log_level_to_color(int level) {
  if (!log_is_level(level)) {
    return NULL;
  }
  
  return level_colors[level - LOG_TRACE];
} // log_level_to_color()

#endif // #ifdef LOG_USE_COLOR

/**
 * @brief Get the string name for a level.
 * 
 * @note  **DO NOT free()** the returned strings! They're elements of an
 *        internal array, and we need them around.
 * 
 * @return char * String like "DEBUG" (`NULL`-termianted). **DO NOT** free()
 *                them!
 * @return char * `NULL` when `level` is not valid.
 */
const char *
log_level_to_name(int level) {
  if (!log_is_level(level)) {
    return NULL;
  }
  
  return level_names[ level - LOG_TRACE ];
} // log_level_to_name()

/**
 * @brief Convert log level name to level integer (that you can use in in
 * log_set_level()).
 * 
 * @param name 
 *    When up-cased, should be one of level_names.
 * 
 * @return int 
 *    Log level integer, or BAD_LEVEL on error (logs an error as well).
 */
int
log_name_to_level(char* name) {
  char *upcased;
  unsigned char *tmp_ptr = NULL;

  // Make an upper-case copy
  upcased = strdup(name); 
  tmp_ptr = (unsigned char *)upcased;
  while(*tmp_ptr) {
      *tmp_ptr = toupper(*tmp_ptr);
      tmp_ptr++;
  }
  
  // Iterate through each level name, checking if it's the same as the
  // upper-case copy of `name`, and return the level if it is.
  for (int level = LOG_TRACE; level <= LOG_FATAL; level++) {
    if (0 == strcmp(log_level_to_name(level), upcased)) {
      return level;
    }
  }
  
  log_error("Level name '%s' not found", upcased);
  return BAD_LEVEL;
} // log_name_to_level()

/**
 * @brief Accessor for the string versions of level integers. Used for comparing
 *        inputs.
 * 
 * Initializes the values first if they haven't been.
 * 
 * @return char **  Array of pointers to strings.
 */
char **
log_level_strings() {  
  int num_levels;
  int level;
  
  // If level_strings is NULL then create it
  if (NULL == level_strings) {    
    num_levels = LOG_FATAL - LOG_TRACE;
    
    level_strings = malloc(num_levels * sizeof(char *));
    
    for(level = LOG_TRACE; level <= LOG_FATAL; level++) {
      level_strings[ level - LOG_TRACE ] = int_to_string(level);
    }
  }
  
  return level_strings;
} // log_level_strings()

/**
 * @brief Convert a level to a string.
 * 
 * @note  **DO NOT** free() the returned string!
 *        
 *        Internally uses a pre-populated array of strings from it returns a 
 *        reference.
 * 
 * @return char * @parblock
 *   String version of the level integer. Again, **DO NOT EVER 
 *   free()**!
 *    
 *   If `level` is *not* a valid level returns `NULL`.
 * @endparblock
 */
char *
log_level_to_string(int level) {
  if (!log_is_level(level)) {
    return NULL;
  }
  
  return log_level_strings()[level - LOG_TRACE];
} // log_level_to_string()


// State
// ---------------------------------------------------------------------------
// 

/**
 * @brief Get the string version of the current level.
 * 
 * @note  **DO NOT free()** the returned strings! They're elements of an
 *        internal array, and we need them around.
 * 
 * @return const char * `NULL`-terminated string like "DEBUG". **DO NOT**
 *                      free()!
 */
const char *
log_get_level_name() {
  return log_level_to_name(L.level);
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
  if (level != BAD_LEVEL) {
    log_set_level(level);
  }
  return level;
}

FILE
*log_get_fp() {
  return L.fp;
}

void
log_set_fp(FILE *fp) {
  L.fp = fp;
}

int
log_get_level() {
  return L.level;
}

/**
 * @brief Set the log level.
 * 
 * If `level` is not valid an error is logged and the log level remains as
 * before.
 * 
 * @param level 
 */
void
log_set_level(int level) {
  if (!log_is_level(level)) {
    log_error("Tried to set bad log level %d", level);
    return;
  }
  L.level = level;
}

/**
 * @brief Is the logger in "quiet" mode - where logging to stderr is silenced,
 *        though file logging continues if configured (see log_get_fp() /
 *        log_set_fp()).
 * 
 * @return bool
 */
bool
log_get_quiet() {
  return L.quiet;
}

/**
 * @brief Sets "quiet" mode - where logging to stderr is silenced, though file
 *        logging continues if configured (see log_get_fp() / log_set_fp()).
 * 
 * @param enable State to set: on (true) or off (false).
 */
void
log_set_quiet(bool enable) {
  L.quiet = enable;
}

void
log_set_udata(void *udata) {
  L.udata = udata;
}

void
log_set_lock(log_LockFn fn) {
  L.lock = fn;
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
 *    Level that was set, or BAD_LEVEL on failure.
 */
int
log_set_level_from_string(char* string) {
  size_t length = strlen(string);
  
  if (0 == length) {
    log_error("Received empty string");
    return BAD_LEVEL;
  }
  
  for (int level = LOG_TRACE; level <= LOG_FATAL; level++) {
    if (0 == strcmp(string, log_level_to_string(level))) {
      log_set_level(level);
      return level;
    }
  }
  
  return log_set_level_by_name(string);
} // log_set_level_from_string()


// Doin' Stuff
// ---------------------------------------------------------------------------

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
void
log_init_from_env(void) {
  if (has_init_from_env) {
    return;
  }
  
  char *value = getenv(LOG_LEVEL_ENV_VAR);
  
  if (value != NULL) {
    log_set_level_from_string(value);
  }
  
  has_init_from_env = 1;
} // log_init_from_env()

/**
 * @brief Does the actual logging of a message. You should not want or need to 
 *        call this function directly - use the log_trace(), log_debug(), etc.
 *        macros.
 * 
 * @param level Level of the message. Note that is is **NOT VALIDATED**, and 
 *              passing a bad level is likely to have bad consequences.
 * @param file  File name to cite in the log.
 * @param line  Line number to cite in the log.
 * @param fmt   The format string for the message (printf-style).
 * @param ...   Arguments to substitute into `fmt`.
 */
void
log_log(int level, const char *file, int line, const char *fmt, ...) {
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
      buf, log_level_to_color(level), log_level_to_name(level), file, line);
#else
    fprintf(  stderr,
              "%s %-5s %s:%d: ",
              buf,
              log_level_to_name(level),
              file,
              line );
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
    fprintf(L.fp, "%s %-5s %s:%d: ", buf, log_level_to_name(level), file, line);
    va_start(args, fmt);
    vfprintf(L.fp, fmt, args);
    va_end(args);
    fprintf(L.fp, "\n");
    fflush(L.fp);
  }

  /* Release lock */
  unlock();
} // log_log()
