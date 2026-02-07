#include "NanoOsUser.h"
#include "libstring.h"

/// @fn void* getenv(void *args)
///
/// @brief Implementation of the standard C getenv fundtion for NanoOs.
///
/// @param args The name of the environment variable to retrive, cast to a
///   void*.
///
/// @return Returns a pointer to the value of the named environment variable on
/// success, NULL on failure.
void* getenv(void *args) {
  const char *name = (char*) args;
  char **env = overlayMap.header.env;
  if ((name == NULL) || (*name == '\0') || (env == NULL)) {
    return NULL;
  }

  size_t nameLen = libStrlen(name);
  char *value = NULL;
  for (int ii = 0; env[ii] != NULL; ii++) {
    if ((libStrncmp(env[ii], name, nameLen) == 0) && env[ii][nameLen] == '=') {
      value = &env[ii][nameLen + 1];
      break;
    }
  }

  return value;
}

