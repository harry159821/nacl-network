/*
 * Copyright 2008, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/** @file
 * Defines the API in the
 * <a href="group___pthread.html">Pthread library</a>
 *
 * @addtogroup Pthread
 * @{
 */

#ifndef _PTHREAD_H
#ifndef DOXYGEN_SHOULD_SKIP_THIS
#define _PTHREAD_H 1
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <sys/nacl_nice.h>
#include <sys/queue.h>
#include <sys/types.h>
/* NOTE: we assume the header file is the right one for the target */
/* NOTE: For x86 we rely on the default 32 bit behavior of
         src/include/linux/x86/atomic_ops.h */
/* TODO(robertm): make this less of a hack */

#include "atomic_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

struct timespec;

/** Fast mutex type; for use with pthread_mutexattr_settype() */
#define PTHREAD_MUTEX_FAST_NP 0
/** Recursive mutex type; for use with pthread_mutexattr_settype() */
#define PTHREAD_MUTEX_RECURSIVE_NP 1
/** Error-checking mutex type; for use with pthread_mutexattr_settype() */
#define PTHREAD_MUTEX_ERRORCHECK_NP 2

/*
 * The layout of pthread_mutex_t and the static initializers are redefined
 * in newlib's sys/lock.h file (including this file from sys/lock.h will
 * cause include conflicts). When changing one of the definitions, make sure to
 * change the second one.
 */
/**
 * A structure representing a thread mutex. It should be considered an
 * opaque record; the names of the fields can change anytime.
 */
typedef struct {
  /** Initialization spinlock */
  int spinlock;

  /**
   * The kind of mutex:
   * PTHREAD_MUTEX_FAST_NP, PTHREAD_MUTEX_RECURSIVE_NP,
   * or PTHREAD_MUTEX_ERRORCHECK_NP
   */
  int mutex_type;

  /** ID of the thread that owns the mutex */
  uint32_t owner_thread_id;

  /** Recursion depth counter for recursive mutexes */
  uint32_t recursion_counter;

  /** Handle to the system-side mutex */
  int mutex_handle;
} pthread_mutex_t;

/**
 * A structure representing mutex attributes. It should be considered an
 * opaque record and accessed only using pthread_mutexattr_settype().
 */
typedef struct {
  /**
   * The kind of mutex:
   * PTHREAD_MUTEX_FAST_NP, PTHREAD_MUTEX_RECURSIVE_NP,
   * or PTHREAD_MUTEX_ERRORCHECK_NP
   */
  int kind;
} pthread_mutexattr_t;

/**
 * A structure representing a condition variable. It should be considered an
 * opaque record; the names of the fields can change anytime.
 */
typedef struct {
  int spinlock; /**< Initialization spinlock */
  int handle; /**< Handle to the system-side condition variable */
} pthread_cond_t;

/**
 * A structure representing condition variable attributes. Currently
 * Native Client condition variables have no attributes.
 */
typedef struct {
  int dummy; /**< Reserved; condition variables don't have attributes */
} pthread_condattr_t;

/** A value that represents an uninitialized handle. */
#define NC_INVALID_HANDLE -1

/** Maximum valid thread ID value. */
#define MAX_THREAD_ID (0xfffffffe)

/** Illegal thread ID value. */
#define NACL_PTHREAD_ILLEGAL_THREAD_ID (0xffffffff)

/** Statically initializes a pthread_mutex_t representing a recursive mutex. */
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP \
    {0, 1, NACL_PTHREAD_ILLEGAL_THREAD_ID, 0, NC_INVALID_HANDLE}
/** Statically initializes a pthread_mutex_t representing a fast mutex. */
#define PTHREAD_MUTEX_INITIALIZER \
    {0, 0, NACL_PTHREAD_ILLEGAL_THREAD_ID, 0, NC_INVALID_HANDLE}
/**
 * Statically initializes a pthread_mutex_t representing an
 * error-checking mutex.
 */
#define PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP \
    {0, 2, NACL_PTHREAD_ILLEGAL_THREAD_ID, 0, NC_INVALID_HANDLE}
/** Statically initializes a condition variable (pthread_cond_t). */
#define PTHREAD_COND_INITIALIZER {0, NC_INVALID_HANDLE}



/* Functions for mutex handling.  */

/** @nqPosix
 * Initializes a mutex using attributes in mutex_attr, or using the
 * default values if the latter is NULL.
 *
 * @linkPthread
 *
 * @param mutex The address of the mutex structure to be initialized.
 * @param mutex_attr The address of the attributes structure.
 *
 * @return 0 upon success, 1 otherwise
 */
extern int pthread_mutex_init(pthread_mutex_t *mutex,
                              pthread_mutexattr_t *mutex_attr);

/** @nqPosix
* Destroys a mutex.
*
* @linkPthread
*
* @param mutex The address of the mutex structure to be destroyed.
*
* @return 0 upon success, non-zero error code otherwise
*/
extern int pthread_mutex_destroy(pthread_mutex_t *mutex);

/** @nqPosix
* Tries to lock a mutex.
*
* @linkPthread
*
* @param mutex The address of the mutex structure to be locked.
*
* @return 0 upon success, EBUSY if the mutex is locked by another thread,
* non-zero error code otherwise.
*/
extern int pthread_mutex_trylock(pthread_mutex_t *mutex);

/** @nqPosix
* Locks a mutex.
*
* @linkPthread
*
* @param mutex The address of the mutex structure to be locked.
*
* @return 0 upon success, non-zero error code otherwise.
*/
extern int pthread_mutex_lock(pthread_mutex_t *mutex);

/* TODO(gregoryd) - depends on implementation */
#if 0
/* Wait until lock becomes available, or specified time passes. */
/* TODO(gregoryd): consider implementing this function. */
extern int pthread_mutex_timedlock(pthread_mutex_t *mutex,
                                   struct timespec *abstime);
#endif

/** @nqPosix
* Unlocks a mutex.
*
* @linkPthread
*
* @param mutex The address of the mutex structure to be unlocked.
*
* @return 0 upon success, non-zero error code otherwise.
*/
extern int pthread_mutex_unlock(pthread_mutex_t *mutex);

/* Mutex attributes manipulation */

/** @nqPosix
* Initializes mutex attributes.
*
* @linkPthread
*
* @param attr The address of the attributes structure to be initialized.
*
* @return 0.
*/
extern int pthread_mutexattr_init(pthread_mutexattr_t *attr);

/** @nqPosix
* Destroys mutex attributes structure.
*
* @linkPthread
*
* @param attr The address of the attributes structure to be destroyed.
*
* @return 0.
*/
extern int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);

/** @nqPosix
* Sets the mutex type: fast, recursive or error-checking.
*
* @linkPthread
*
* @param attr The address of the attributes structure.
* @param kind PTHREAD_MUTEX_FAST_NP, PTHREAD_MUTEX_RECURSIVE_NP or
* PTHREAD_MUTEX_ERRORCHECK_NP.
*
* @return 0 on success, -1 for illegal values of kind.
*/
extern int pthread_mutexattr_settype(pthread_mutexattr_t *attr,
                                     int kind);

/** @nqPosix
* Gets the mutex type: fast, recursive or error-checking.
*
* @linkPthread
*
* @param attr The address of the attributes structure.
* @param kind Pointer to the location where the mutex kind value is copied.
*
* @return 0.
*/
extern int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr,
                                     int *kind);

/* Functions for handling conditional variables.  */

/** @nqPosix
* Initializes a condition variable.
*
* @linkPthread
*
* @param cond Pointer to the condition variable structure.
* @param cond_attr Pointer to the attributes structure, should be NULL as
* Native Client does not support any attributes for a condition variable at
* this stage.
*
* @return 0 for success, 1 otherwise.
*/
extern int pthread_cond_init(pthread_cond_t *cond,
                             pthread_condattr_t *cond_attr);

/** @nqPosix
* Destroys a condition variable.
*
* @linkPthread
*
* @param cond Pointer to the condition variable structure.
*
* @return 0 for success, non-zero error code otherwise.
*/
extern int pthread_cond_destroy(pthread_cond_t *cond);

/** @nqPosix
* Signals a condition variable, waking up one of the threads waiting on it.
*
* @linkPthread
*
* @param cond Pointer to the condition variable structure.
*
* @return 0 for success, non-zero error code otherwise.
*/
extern int pthread_cond_signal(pthread_cond_t *cond);

/** @nqPosix
* Wakes up all threads waiting on a condition variable.
*
* @linkPthread
*
* @param cond Pointer to the condition variable structure.
*
* @return 0 for success, non-zero error code otherwise.
*/
extern int pthread_cond_broadcast(pthread_cond_t *cond);

/** @nqPosix
* Waits for a condition variable to be signaled or broadcast.
*
* @linkPthread
*
* @param cond Pointer to the condition variable structure.
* @param mutex Pointer to the mutex structure. The mutex is assumed to be locked
* when this function is called.
*
* @return 0 for success, non-zero error code otherwise.
*/
extern int pthread_cond_wait(pthread_cond_t *cond,
                             pthread_mutex_t *mutex);

/** @nqPosix
* Waits for condition variable cond to be signaled or broadcast until
* abstime.
*
* @linkPthread
*
* @param cond Pointer to the condition variable structure.
* @param mutex Pointer to the mutex structure. The mutex is assumed to be locked
* when this function is called.
* @param abstime Absolute time specification; zero is the beginning of the epoch
* (00:00:00 GMT, January 1, 1970).
*
* @return 0 for success, non-zero error code otherwise.
*/
int pthread_cond_timedwait_abs(pthread_cond_t *cond,
                               pthread_mutex_t *mutex,
                               struct timespec *abstime);

 /** @nqPosix
* Waits for condition variable cond to be signaled or broadcast; wait time is
* limited by reltime.
*
* @linkPthread
*
* @param cond Pointer to the condition variable structure.
* @param mutex Pointer to the mutex structure. The mutex is assumed to be locked
* when this function is called.
* @param reltime Time specification, relative to the current time.
*
* @return 0 for success, non-zero error code otherwise.
*/
int pthread_cond_timedwait_rel(pthread_cond_t *cond,
                               pthread_mutex_t *mutex,
                               struct timespec *reltime);

/**
 * Defined for POSIX compatibility; pthread_cond_timedwait() is actually
 * a macro calling pthread_cond_timedwait_abs().
 */
#define pthread_cond_timedwait pthread_cond_timedwait_abs


/* Threads */
/** Thread entry function type. */
typedef void *(*nc_thread_function)(void *p);
/** Thread identifier type. */
typedef uint32_t pthread_t;

/** A structure representing thread attributes. */
typedef struct {
  int joinable; /**< 1 if the thread is joinable, 0 otherwise */
} pthread_attr_t;


/** Joinable thread type; for use with pthread_attr_setdetachstate(). */
#define PTHREAD_CREATE_JOINABLE 1
/** Detached thread type; for use with pthread_attr_setdetachstate(). */
#define PTHREAD_CREATE_DETACHED 0

/* Thread functions  */

/** @nqPosix
* Creates a thread.
*
* @linkPthread
*
* @param[out] thread_id A pointer to the location where the identifier of the
* newly created thread is stored on success.
* @param attr Thread attributes structure.
* @param start_routine Thread function.
* @param arg A single argument that is passed to the thread function.
*
* @return 0 for success, non-zero error code otherwise.
*/
extern int pthread_create(pthread_t *thread_id,
                          pthread_attr_t *attr,
                          void *(*start_routine) (void *p),
                          void *arg);

/** @nqPosix
* Obtains the identifier of the current thread.
*
* @linkPthread
*
* @return Thread ID of the current thread.
*/
extern pthread_t pthread_self(void);

/** @nqPosix
* Compares two thread identifiers.
*
* @linkPthread
*
* @param thread1 Thread ID of thread A.
* @param thread2 Thread ID of thread B.
*
* @return 1 if both identifiers belong to the same thread, 0 otherwise.
*/
extern int pthread_equal(pthread_t thread1, pthread_t thread2);

/** @nqPosix
* Terminates the calling thread.
*
* @linkPthread
*
* @param retval Return value of the thread.
*
* @return The function never returns.
*/
extern void pthread_exit(void *retval);

/** @nqPosix
* Makes the calling thread wait for termination of another thread.
*
* @linkPthread
*
* @param th The identifier of the thread to wait for.
* @param thread_return If not NULL, points to the location where the return
* value of the terminated thread is stored upon completion.
*
* @return 0 on success, non-zero error code otherwise.
*/
extern int pthread_join(pthread_t th, void **thread_return);

/** @nqPosix
* Indicates that the specified thread is never to be joined with pthread_join().
* The resources of that thread will therefore be freed immediately when it
* terminates, instead of waiting for another thread to perform pthread_join()
* on it.
*
* @linkPthread
*
* @param th Thread identifier.
*
* @return 0 on success, non-zero error code otherwise.
*/
extern int pthread_detach(pthread_t th);

/** @nqPosix
* Indicates that the calling thread is to receive special consideration
* by the scheduler as indicated by parameter nice. Suggested values are
* NICE_REALTIME, NICE_NORMAL, NICE_BACKGROUND, defined above. The
* implementation of this subroutine is platform-specific. Implementations
* should respect the sign of the nice parameter, and may respect the
* magnitude.
*
* @linkPthread
*
* @param nice Nice value
*
* @return 0 on success, non-zero error code otherwise.
*/
extern int nacl_thread_nice(const int nice);

/* Functions for handling thread attributes.  */
/** @nqPosix
* Initializes thread attributes structure attr with default attributes
* (detachstate is PTHREAD_CREATE_JOINABLE).
*
* @linkPthread
*
* @param attr Pointer to thread attributes structure.
*
* @return 0 on success, non-zero error code otherwise.
*/
extern int pthread_attr_init(pthread_attr_t *attr);

/** @nqPosix
* Destroys a thread attributes structure.
*
* @linkPthread
*
* @param attr Pointer to thread attributes structure.
*
* @return 0 on success, non-zero error code otherwise.
*/
extern int pthread_attr_destroy(pthread_attr_t *attr);

/** @nqPosix
* Sets the detachstate attribute in thread attributes.
*
* @linkPthread
*
* @param attr Pointer to thread attributes structure.
* @param detachstate Value to be set, determines whether the thread is joinable.
*
* @return 0 on success, non-zero error code otherwise.
*/
extern int pthread_attr_setdetachstate(pthread_attr_t *attr,
                                       int detachstate);

/** @nqPosix
* Gets the detachstate attribute from thread attributes.
*
* @linkPthread
*
* @param attr Pointer to thread attributes structure.
* @param detachstate Location where the value of `detachstate` is stored upon
* successful completion.
*
* @return 0 on success, non-zero error code otherwise.
*/
extern int pthread_attr_getdetachstate(pthread_attr_t *attr,
                                       int *detachstate);

/* Functions for handling thread-specific data.  */

/** Thread-specific key identifier type */
typedef int pthread_key_t;

/** Number of available keys for thread-specific data. */
#define PTHREAD_KEYS_MAX 512

/** @nqPosix
* Creates a key value identifying a location in the thread-specific
* data area.
*
* @linkPthread
*
* @param key Pointer to the location where the value of the key is stored upon
* successful completion.
* @param destr_function Pointer to a cleanup function that is called if the
* thread terminates while the key is still allocated.
*
* @return 0 on success, non-zero error code otherwise.
*/
extern int pthread_key_create(pthread_key_t *key,
                              void (*destr_function)(void *p));


/** @nqPosix
* Destroys a thread-specific data key.
*
* @linkPthread
*
* @param key Key value, previously obtained using pthread_key_create().
*
* @return 0 on success, non-zero error code otherwise.
*/
extern int pthread_key_delete(pthread_key_t key);

/** @nqPosix
* Stores a value in the thread-specific data slot identified by a key value.
*
* @linkPthread
*
* @param key Key value, previously obtained using pthread_key_create().
* @param pointer The value to be stored.
*
* @return 0 on success, non-zero error code otherwise.
*/
extern int pthread_setspecific(pthread_key_t key,
                               const void *pointer);

/** @nqPosix
* Gets the value currently stored at the thread-specific data slot
* identified by the key.
*
* @linkPthread
*
* @param key Key value, previously obtained using pthread_key_create().
*
* @return The value that was previously stored using pthread_setspecific is
* returned on success, otherwise NULL.
*/
extern void *pthread_getspecific(pthread_key_t key);

/**
 * A structure describing a control block
 * used with the pthread_once() function.
 * It should be considered an opaque record;
 * the names of the fields can change anytime.
 */
typedef struct {
  /** A flag: 1 if the function was already called, 0 if it wasn't */
  AtomicWord      done;

  /** Synchronization lock for the flag */
  pthread_mutex_t lock;
} pthread_once_t;

/** Static initializer for pthread_once_t. */
#define PTHREAD_ONCE_INIT {0, PTHREAD_MUTEX_INITIALIZER}

/** @nqPosix
* Ensures that a piece of initialization code is executed at most once.
*
* @linkPthread
*
* @param __once_control Points to a static or extern variable statically
* initialized to PTHREAD_ONCE_INIT.
* @param __init_routine A pointer to the initialization function.
*
* @return 0.
*/
extern int pthread_once(pthread_once_t *__once_control,
                        void (*__init_routine)(void));


/*
 * NOTE: this is only declared here to shut up
 * some warning in the c++ system header files.
 * We do not define this function anywhere.
 */

extern int pthread_cancel(pthread_t th);

/**
* @} End of PTHREAD group
*/

#ifdef __cplusplus
}
#endif

#endif  /* pthread.h */
