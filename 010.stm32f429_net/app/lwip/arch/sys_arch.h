/*
 * sys_arch.h - LwIP OS abstraction layer for FreeRTOS (V11).
 *
 * Included by lwip/sys.h when NO_SYS=0.  All types/functions are
 * implemented in app/lwip/sys_arch.c on top of the FreeRTOS kernel.
 */
#ifndef LWIP_SYS_ARCH_H
#define LWIP_SYS_ARCH_H

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "lwip/err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- types ---- */
typedef SemaphoreHandle_t sys_sem_t;
typedef SemaphoreHandle_t sys_mutex_t;
typedef QueueHandle_t sys_mbox_t;
typedef TaskHandle_t sys_thread_t;
typedef int sys_prot_t;

/* ---- critical sections (FreeRTOS) ---- */
#define SYS_ARCH_DECL_PROTECT(lev)  sys_prot_t lev
#define SYS_ARCH_PROTECT(lev)       (lev) = sys_arch_protect()
#define SYS_ARCH_UNPROTECT(lev)     sys_arch_unprotect((lev))

/* ---- function declarations (implemented in sys_arch.c) ---- */
sys_prot_t sys_arch_protect(void);
void sys_arch_unprotect(sys_prot_t pval);

/* ---- validity helpers (LwIP requires these when NO_SYS=0) ---- */
#define sys_mbox_valid(mbox)         (((mbox) != NULL) && ((*(mbox)) != NULL))
#define sys_mbox_set_invalid(mbox)   ((*(mbox)) = NULL)
#define sys_sem_valid(sem)           (((sem) != NULL) && ((*(sem)) != NULL))
#define sys_sem_set_invalid(sem)     ((*(sem)) = NULL)
#define sys_mutex_valid(mutex)       (((mutex) != NULL) && ((*(mutex)) != NULL))
#define sys_mutex_set_invalid(mutex) ((*(mutex)) = NULL)

err_t sys_mbox_new(sys_mbox_t *mbox, int size);
void sys_mbox_free(sys_mbox_t *mbox);
void sys_mbox_post(sys_mbox_t *mbox, void *msg);
err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg);
err_t sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg);
u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout);
u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg);

err_t sys_sem_new(sys_sem_t *sem, u8_t count);
void sys_sem_free(sys_sem_t *sem);
void sys_sem_signal(sys_sem_t *sem);
void sys_sem_signal_isr(sys_sem_t *sem);
u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout);

err_t sys_mutex_new(sys_mutex_t *mutex);
void sys_mutex_free(sys_mutex_t *mutex);
void sys_mutex_lock(sys_mutex_t *mutex);
void sys_mutex_unlock(sys_mutex_t *mutex);

sys_thread_t sys_thread_new(const char *name, void (*thread)(void *arg),
                            void *arg, int stacksize, int prio);

u32_t sys_now(void);
u32_t sys_jiffies(void);
void sys_msleep(u32_t ms);

void sys_init(void);

#ifdef __cplusplus
}
#endif

#endif /* LWIP_SYS_ARCH_H */
