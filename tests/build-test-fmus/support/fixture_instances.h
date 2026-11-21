#ifndef LIBCOSIM_FIXTURE_INSTANCES_H
#define LIBCOSIM_FIXTURE_INSTANCES_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Each fixture binary owns its instances until FreeInstance or library unload.
// The importer cannot call FreeInstance after an FMI Fatal status.
bool fixture_track_instance(void* instance, void (*destroy)(void*));
void fixture_free_instance(void* instance);

#ifdef __cplusplus
}
#endif

#endif
