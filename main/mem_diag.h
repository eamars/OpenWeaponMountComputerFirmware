#ifndef MEM_DIAG_H
#define MEM_DIAG_H

#ifdef __cplusplus
extern "C" {
#endif

void mem_diag_log_checkpoint(const char *checkpoint);
void mem_diag_install_alloc_failure_hook(void);
void mem_diag_start_stack_report(void);

#ifdef __cplusplus
}
#endif

#endif // MEM_DIAG_H
