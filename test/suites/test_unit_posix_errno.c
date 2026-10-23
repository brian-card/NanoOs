///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_unit_posix_errno.c
///
/// @brief             Unit tests (no kernel boot) for posixErrnoToNanoOs, the
///                    simulator HAL's Linux to NanoOs errno mapping.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <errno.h>

int posixErrnoToNanoOs(int linuxErrno);

NANO_OS_TEST(unit_posix_errno, every_shared_name_maps_to_its_nanoos_value) {
  // Expected values are those in src/user/NanoOsErrno.h, which can't be
  // included alongside the host's errno.h.
  static const int mapping[][2] = {
    { EBUSY,         2 }, { ENOMEM,        3 }, { EACCES,        4 },
    { EINVAL,        5 }, { EIO,           6 }, { ENOSPC,        7 },
    { ENOENT,        8 }, { ENOTEMPTY,     9 }, { EOVERFLOW,    10 },
    { EFAULT,       11 }, { ENAMETOOLONG, 12 }, { EBADF,        13 },
    { ENODEV,       14 }, { ENOTTY,       15 }, { ERANGE,       16 },
    { ELOOP,        17 }, { ETIMEDOUT,    18 }, { ENOEXEC,      19 },
    { ENOTSUP,      20 }, { ENXIO,        21 }, { EPERM,        22 },
    { ESRCH,        23 }, { EAGAIN,       24 }, { ENOTDIR,      25 },
    { EEXIST,       26 },
  };
  for (size_t ii = 0; ii < sizeof(mapping) / sizeof(mapping[0]); ii++) {
    NANO_OS_ASSERT_EQ_INT(mapping[ii][1], posixErrnoToNanoOs(mapping[ii][0]));
  }
}

NANO_OS_TEST(unit_posix_errno, zero_stays_zero) {
  NANO_OS_ASSERT_EQ_INT(0, posixErrnoToNanoOs(0));
}

NANO_OS_TEST(unit_posix_errno, enosys_maps_to_enotsup) {
  NANO_OS_ASSERT_EQ_INT(20, posixErrnoToNanoOs(ENOSYS));
}

NANO_OS_TEST(unit_posix_errno, unknown_values_map_to_eother) {
  NANO_OS_ASSERT_EQ_INT(1, posixErrnoToNanoOs(EINTR));
  NANO_OS_ASSERT_EQ_INT(1, posixErrnoToNanoOs(EHWPOISON));
}
