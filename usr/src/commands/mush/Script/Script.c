////////////////////////////////////////////////////////////////////////////////
//
//                       Copyright (c) 2026 Brian Card
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
//                                 Brian Card
//                       https://github.com/brian-card
//
////////////////////////////////////////////////////////////////////////////////

// Doxygen marker
/// @file
///
/// @brief Overlay for running scripts.  Every foreground command replaces the
/// shell, so the running scripts are kept in the environment, which is all
/// that survives until the shell is restarted.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mush.h"
#include "NanoOsUtils.h"

/// @def MUSH_MAX_SCRIPT_DEPTH
///
/// @brief The maximum number of scripts that can be running inside each other.
#define MUSH_MAX_SCRIPT_DEPTH 9

/// @def MUSH_SCRIPT_DIGIT
///
/// @brief The index of the digit in the name of a script's variable.
#define MUSH_SCRIPT_DIGIT 11

/// @def MUSH_ENV_DIGIT
///
/// @brief The index of the digit in the name of a saved environment's variable.
#define MUSH_ENV_DIGIT 8

/// @def MUSH_ENV_SEPARATOR
///
/// @brief The string between the variables of a saved environment.
#define MUSH_ENV_SEPARATOR "\x1f"

/// @fn static int scriptDepth(char *scriptName)
///
/// @brief Count the running scripts and name the variable of the innermost one.
///
/// @param scriptName A buffer of sizeof("MUSH_SCRIPT0") bytes that receives
///   the name of the innermost script's variable.
///
/// @return Returns the number of running scripts.
static int scriptDepth(char *scriptName) {
  strcpy(scriptName, "MUSH_SCRIPT1");
  int depth = 0;
  while ((depth < MUSH_MAX_SCRIPT_DEPTH) && (getenv(scriptName) != NULL)) {
    depth++;
    scriptName[MUSH_SCRIPT_DIGIT] = '1' + depth;
  }
  scriptName[MUSH_SCRIPT_DIGIT] = '0' + depth;
  return depth;
}

/// @fn static int saveEnvironment(char digit)
///
/// @brief Save every variable that isn't a script's own into MUSH_ENV<digit>.
///
/// @param digit The digit of the script the environment is saved for.
///
/// @return Returns 0 on success, -1 on failure.
static int saveEnvironment(char digit) {
  size_t size = 1;
  for (char **variable = environ; (variable != NULL) && (*variable != NULL);
    variable++
  ) {
    if (strncmp(*variable, "MUSH_", 5) != 0) {
      size += strlen(*variable) + 1;
    }
  }
  char *saved = (char*) malloc(size);
  if (saved == NULL) {
    return -1;
  }
  *saved = '\0';
  for (char **variable = environ; (variable != NULL) && (*variable != NULL);
    variable++
  ) {
    if (strncmp(*variable, "MUSH_", 5) != 0) {
      if (*saved != '\0') {
        strcat(saved, MUSH_ENV_SEPARATOR);
      }
      strcat(saved, *variable);
    }
  }

  char envName[] = "MUSH_ENV0";
  envName[MUSH_ENV_DIGIT] = digit;
  int returnValue = setenv(envName, saved, true);
  free(saved);
  return returnValue;
}

/// @fn static void restoreEnvironment(const char *envName)
///
/// @brief Replace every variable that isn't a script's own with the ones saved
/// in a saved environment's variable.
///
/// @param envName The name of the saved environment's variable.
///
/// @return This function returns no value.
static void restoreEnvironment(const char *envName) {
  char *value = getenv(envName);
  if (value == NULL) {
    return;
  }
  // setenv and unsetenv move the environment, value included.
  char *saved = (char*) malloc(strlen(value) + 1);
  if (saved == NULL) {
    fputs("mush: Could not restore the environment\n", stderr);
    return;
  }
  strcpy(saved, value);

  size_t ii = 0;
  while ((environ != NULL) && (environ[ii] != NULL)) {
    if (strncmp(environ[ii], "MUSH_", 5) == 0) {
      ii++;
      continue;
    }
    size_t nameLength = strcspn(environ[ii], "=");
    char *name = (char*) malloc(nameLength + 1);
    if (name == NULL) {
      break;
    }
    strncpy(name, environ[ii], nameLength);
    name[nameLength] = '\0';
    if (unsetenv(name) != 0) {
      ii++;
    }
    free(name);
  }

  char *entry = saved;
  while (*entry != '\0') {
    char *end = &entry[strcspn(entry, MUSH_ENV_SEPARATOR)];
    char *next = (*end == '\0') ? end : &end[1];
    *end = '\0';
    char *equals = strchr(entry, '=');
    if (equals != NULL) {
      *equals = '\0';
      setenv(entry, &equals[1], true);
    }
    entry = next;
  }
  free(saved);
}

/// @fn static void endInnermostScript(const char *scriptName)
///
/// @brief Stop running the innermost script, putting back the environment it
/// started with if it saved one.
///
/// @param scriptName The name of the innermost script's variable.
///
/// @return This function returns no value.
static void endInnermostScript(const char *scriptName) {
  char envName[] = "MUSH_ENV0";
  envName[MUSH_ENV_DIGIT] = scriptName[MUSH_SCRIPT_DIGIT];
  restoreEnvironment(envName);
  unsetenv(envName);
  unsetenv(scriptName);
}

/// @fn void* readScriptLine(void *args)
///
/// @brief Read the next line of the innermost running script, ending the
/// scripts that have no lines left.  The position after the line is saved
/// before the line runs, so a command that replaces the shell doesn't run
/// again when the shell is restarted.
///
/// @param args A pointer to a ScriptArgs structure, cast to a void*.
///
/// @return Returns args if a line was read, NULL if no script is running.
void* readScriptLine(void *args) {
  ScriptArgs *scriptArgs = (ScriptArgs*) args;
  char scriptName[sizeof("MUSH_SCRIPT0")];
  while (scriptDepth(scriptName) > 0) {
    char *end = NULL;
    char *value = getenv(scriptName);
    long offset = strtol(value, &end, 10);
    long lineNumber = strtol(end, &end, 10);
    if (*end != ' ') {
      endInnermostScript(scriptName);
      continue;
    }
    const char *path = &end[1];

    bool haveLine = false;
    FILE *script = fopen(path, "r");
    if (script == NULL) {
      fputs(path, stderr);
      fprintf(stderr, ": %s\n", strerror(errno));
    } else {
      if (fseek(script, offset, SEEK_SET) == 0) {
        while (fgets(scriptArgs->line, scriptArgs->size, script) != NULL) {
          lineNumber++;
          if ((strchr(scriptArgs->line, '\n') == NULL) && !feof(script)) {
            fputs(path, stderr);
            fprintf(stderr, ": line %ld: too long\n", lineNumber);
            while ((fgets(scriptArgs->line, scriptArgs->size, script) != NULL)
              && (strchr(scriptArgs->line, '\n') == NULL));
            continue;
          }
          haveLine = true;
          break;
        }
      }
      offset = ftell(script);
      fclose(script);
    }

    if (haveLine) {
      size_t size = strlen(path) + 24;
      char *newValue = (char*) malloc(size);
      if (newValue != NULL) {
        snprintf(newValue, size, "%ld %ld %s", offset, lineNumber, path);
        int status = setenv(scriptName, newValue, true);
        free(newValue);
        if (status == 0) {
          scriptArgs->line[strcspn(scriptArgs->line, "\r\n")] = '\0';
          return scriptArgs;
        }
      }
      fputs("mush: Could not save the script's position\n", stderr);
    }
    endInnermostScript(scriptName);
  }

  return NULL;
}

/// @fn void* startScript(void *args)
///
/// @brief Start running a script inside any that are already running.
///
/// @param args A pointer to a ScriptArgs structure, cast to a void*.
///
/// @return Returns args on success, NULL on failure.
void* startScript(void *args) {
  ScriptArgs *scriptArgs = (ScriptArgs*) args;
  if (scriptArgs->inBackground) {
    fputs(scriptArgs->path, stderr);
    fputs(": A script can't run in the background or in a pipeline\n", stderr);
    return NULL;
  }

  char scriptName[sizeof("MUSH_SCRIPT0")];
  int depth = scriptDepth(scriptName);
  if (depth >= MUSH_MAX_SCRIPT_DEPTH) {
    fputs(scriptArgs->path, stderr);
    fputs(": Too many scripts running inside each other\n", stderr);
    return NULL;
  }

  const char *pwd = getenv("PWD");
  if (pwd == NULL) {
    pwd = "/";
  }
  size_t size = strlen(pwd) + strlen(scriptArgs->path) + 6;
  char *value = (char*) malloc(size);
  if (value == NULL) {
    fputs(scriptArgs->path, stderr);
    fprintf(stderr, ": %s\n", strerror(ENOMEM));
    return NULL;
  }
  // The script starts at offset 0, before line 1.
  strcpy(value, "0 0 ");
  char *fullPath = &value[4];
  if (*scriptArgs->path == '/') {
    strcpy(fullPath, scriptArgs->path);
  } else {
    snprintf(fullPath, size - 4, "%s/%s", pwd, scriptArgs->path);
  }
  pathCollapse(fullPath);

  void *returnValue = scriptArgs;
  FILE *script = fopen(fullPath, "r");
  if (script == NULL) {
    if (!scriptArgs->isProfile) {
      fputs(scriptArgs->path, stderr);
      fprintf(stderr, ": %s\n", strerror(errno));
    }
    returnValue = NULL;
    goto exit;
  }
  fclose(script);

  scriptName[MUSH_SCRIPT_DIGIT] = '1' + depth;
  if ((!scriptArgs->isProfile
      && (saveEnvironment(scriptName[MUSH_SCRIPT_DIGIT]) != 0))
    || (setenv(scriptName, value, true) != 0)
  ) {
    fputs(scriptArgs->path, stderr);
    fprintf(stderr, ": %s\n", strerror(ENOMEM));
    endInnermostScript(scriptName);
    returnValue = NULL;
  }

exit:
  free(value);
  return returnValue;
}

/// @fn void* endScript(void *args)
///
/// @brief Stop running the innermost script.
///
/// @param args Unused.
///
/// @return This function always returns NULL.
void* endScript(void *args) {
  (void) args;
  char scriptName[sizeof("MUSH_SCRIPT0")];
  if (scriptDepth(scriptName) > 0) {
    endInnermostScript(scriptName);
  }
  return NULL;
}
