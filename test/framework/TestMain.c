///////////////////////////////////////////////////////////////////////////////
///
/// @file              TestMain.c
///
/// @brief             Entry point for the NanoOs kernel test harness binary.
///                    Wires the kernel runner into the framework and runs the
///                    registered suites.
///
/// Usage:
///   nano-os-test [filter]
///
///   filter   Optional substring; only tests whose "suite/name" contains it
///            are run.
///
///////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <string.h>

#include "NanoOsTest.h"
#include "KernelTestHarness.h"

int main(int argc, char **argv) {
  const char *filter = NULL;
  for (int ii = 1; ii < argc; ii++) {
    if ((strcmp(argv[ii], "-h") == 0) || (strcmp(argv[ii], "--help") == 0)) {
      printf("Usage: %s [filter]\n", argv[0]);
      printf("  filter   run only tests whose \"suite/name\" contains this "
        "substring\n");
      return 0;
    }
    filter = argv[ii];
  }

  nanoOsTestSetKernelRunner(kernelTestRun);

  int failures = nanoOsTestRunAll(filter);
  return (failures == 0) ? 0 : 1;
}
