// newlib_locks.c -- retargetable-lock hooks for newlib builds that need them.
//
// The arm-none-eabi toolchain bundled with arduino-pico builds newlib with
// _RETARGETABLE_LOCKING and leaves these to the platform (Arduino supplies
// them).  AMY's stdio and malloc use pulls them in.  Every newlib lock maps to
// one recursive mutex, which is safe across both cores.  Weak, so a toolchain
// whose newlib already defines them keeps its own.

#include <sys/lock.h>

#ifdef _RETARGETABLE_LOCKING

#include "pico/mutex.h"

auto_init_recursive_mutex(newlib_mutex);

__attribute__((weak)) struct __lock __lock___malloc_recursive_mutex;
__attribute__((weak)) struct __lock __lock___sfp_recursive_mutex;
__attribute__((weak)) struct __lock __lock___sinit_recursive_mutex;
__attribute__((weak)) struct __lock __lock___atexit_recursive_mutex;
__attribute__((weak)) struct __lock __lock___at_quick_exit_mutex;
__attribute__((weak)) struct __lock __lock___env_recursive_mutex;
__attribute__((weak)) struct __lock __lock___tz_mutex;
__attribute__((weak)) struct __lock __lock___dd_hash_mutex;
__attribute__((weak)) struct __lock __lock___arc4random_mutex;

__attribute__((weak)) void __retarget_lock_init(_LOCK_T lock) { (void)lock; }
__attribute__((weak)) void __retarget_lock_init_recursive(_LOCK_T lock) { (void)lock; }
__attribute__((weak)) void __retarget_lock_close(_LOCK_T lock) { (void)lock; }
__attribute__((weak)) void __retarget_lock_close_recursive(_LOCK_T lock) { (void)lock; }

__attribute__((weak)) void __retarget_lock_acquire(_LOCK_T lock) {
    (void)lock;
    recursive_mutex_enter_blocking(&newlib_mutex);
}
__attribute__((weak)) void __retarget_lock_acquire_recursive(_LOCK_T lock) {
    (void)lock;
    recursive_mutex_enter_blocking(&newlib_mutex);
}
__attribute__((weak)) int __retarget_lock_try_acquire(_LOCK_T lock) {
    (void)lock;
    return recursive_mutex_try_enter(&newlib_mutex, NULL);
}
__attribute__((weak)) int __retarget_lock_try_acquire_recursive(_LOCK_T lock) {
    (void)lock;
    return recursive_mutex_try_enter(&newlib_mutex, NULL);
}
__attribute__((weak)) void __retarget_lock_release(_LOCK_T lock) {
    (void)lock;
    recursive_mutex_exit(&newlib_mutex);
}
__attribute__((weak)) void __retarget_lock_release_recursive(_LOCK_T lock) {
    (void)lock;
    recursive_mutex_exit(&newlib_mutex);
}

#endif  // _RETARGETABLE_LOCKING
