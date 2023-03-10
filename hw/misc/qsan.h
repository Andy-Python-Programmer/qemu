#include <stdint.h>
#include "exec/hwaddr.h"
#include "qemu/typedefs.h"

void qsan_validate_access(CPUArchState *env, hwaddr addr, MMUAccessType access_type);