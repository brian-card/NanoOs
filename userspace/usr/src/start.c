#include "NanoOsUser.h"

int main(int argc, char **argv);

void* _start(void *args) {
  MainArgs *mainArgs = (MainArgs*) args;
  return (void*) ((intptr_t) main(mainArgs->argc, mainArgs->argv));
}

