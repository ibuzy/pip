#include <x86intrin.h>
static inline unsigned esp_cpu_get_cycle_count(void) { return (unsigned)__rdtsc(); }
