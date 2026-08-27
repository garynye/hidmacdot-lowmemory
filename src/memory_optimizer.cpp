#include "memory_optimizer.h"

#include <Windows.h>

namespace memory_optimizer {

bool ReleaseIdleHeapCaches() {
    HEAP_OPTIMIZE_RESOURCES_INFORMATION information{};
    information.Version = HEAP_OPTIMIZE_RESOURCES_CURRENT_VERSION;
    return HeapSetInformation(
        nullptr,
        HeapOptimizeResources,
        &information,
        sizeof(information)) != FALSE;
}

} // namespace memory_optimizer
