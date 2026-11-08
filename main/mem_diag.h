#ifndef MEM_DIAG_H
#define MEM_DIAG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void mem_diag_log_checkpoint(const char *checkpoint);
void mem_diag_install_alloc_failure_hook(void);
void mem_diag_start_stack_report(void);

/**
 * @brief Log each task's share of one core over `window_ms` (`cpu_rpt:` lines), blocking the caller meanwhile.
 * Verification builds only (CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS); returns false otherwise or without memory
 */
bool mem_diag_cpu_report(uint32_t window_ms);

#ifdef __cplusplus
}
#endif

#endif // MEM_DIAG_H
