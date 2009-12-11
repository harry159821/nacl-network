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


#include <time.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sched.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "native_client/src/include/portability.h"
#include "native_client/src/include/nacl_platform.h"

#include "native_client/src/shared/platform/nacl_log.h"
#include "native_client/src/shared/platform/nacl_sync_checked.h"
#include "native_client/src/shared/platform/nacl_time.h"

#include "native_client/src/trusted/desc/nacl_desc_base.h"
#include "native_client/src/trusted/desc/nacl_desc_conn_cap.h"
#include "native_client/src/trusted/desc/nacl_desc_imc.h"
#include "native_client/src/trusted/desc/nacl_desc_imc_bound_desc.h"
#include "native_client/src/trusted/desc/nacl_desc_imc_shm.h"
#include "native_client/src/trusted/desc/nacl_desc_io.h"

#include "native_client/src/trusted/service_runtime/nacl_app_thread.h"
#include "native_client/src/trusted/service_runtime/nacl_closure.h"
#include "native_client/src/trusted/service_runtime/nacl_config.h"
#include "native_client/src/trusted/service_runtime/nacl_globals.h"
#include "native_client/src/trusted/service_runtime/nacl_memory_object.h"
#include "native_client/src/trusted/service_runtime/nacl_syscall_handlers.h"
#include "native_client/src/trusted/service_runtime/nacl_syscall_common.h"
#include "native_client/src/trusted/service_runtime/sel_util.h"
#include "native_client/src/trusted/service_runtime/sel_ldr.h"
#include "native_client/src/trusted/service_runtime/sel_memory.h"

#include "native_client/src/trusted/service_runtime/include/bits/mman.h"
#include "native_client/src/trusted/service_runtime/include/bits/nacl_syscalls.h"
#include "native_client/src/trusted/service_runtime/include/machine/_types.h"
#include "native_client/src/trusted/service_runtime/include/sys/errno.h"
#include "native_client/src/trusted/service_runtime/include/sys/fcntl.h"
#include "native_client/src/trusted/service_runtime/include/sys/nacl_imc_api.h"
#include "native_client/src/trusted/service_runtime/include/sys/stat.h"
#include "native_client/src/trusted/service_runtime/include/sys/time.h"
#include "native_client/src/trusted/service_runtime/include/sys/unistd.h"

#include "native_client/src/trusted/service_runtime/linux/nacl_syscall_inl.h"

#if defined(HAVE_SDL)
# include "native_client/src/trusted/service_runtime/nacl_bottom_half.h"
#endif

struct NaClSyscallTableEntry nacl_syscall[NACL_MAX_SYSCALLS] = {{0}};



static int32_t NotImplementedDecoder(struct NaClAppThread *natp) {
  UNREFERENCED_PARAMETER(natp);
  return -NACL_ABI_ENOSYS;
}

static void NaClAddSyscall(int num, int32_t (*fn)(struct NaClAppThread *)) {
  if (nacl_syscall[num].handler != &NotImplementedDecoder) {
    NaClLog(LOG_FATAL, "Duplicate syscall number %d\n", num);
  }
  nacl_syscall[num].handler = fn;
}

/* ====================================================================== */

int32_t NaClSysNull(struct NaClAppThread *natp) {
  UNREFERENCED_PARAMETER(natp);
  return 0;
}

int32_t NaClSysOpen(struct NaClAppThread  *natp,
                    char                  *pathname,
                    int                   flags,
                    int                   mode) {
  return NaClCommonSysOpen(natp, pathname, flags, mode);
}

int32_t NaClSysClose(struct NaClAppThread *natp,
                     int                  d) {
  return NaClCommonSysClose(natp, d);
}

int32_t NaClSysRead(struct NaClAppThread  *natp,
                    int                   d,
                    void                  *buf,
                    size_t                count) {
  return NaClCommonSysRead(natp, d, buf, count);
}

int32_t NaClSysWrite(struct NaClAppThread *natp,
                     int                  d,
                     void                 *buf,
                     size_t               count) {
  return NaClCommonSysWrite(natp, d, buf, count);
}

/* Warning: sizeof(nacl_abi_off_t)!=sizeof(off_t) on OSX */
int32_t NaClSysLseek(struct NaClAppThread *natp,
                     int                  d,
                     nacl_abi_off_t       offset,
                     int                  whence) {
  return NaClCommonSysLseek(natp, d, (off_t)offset, whence);
}

int32_t NaClSysIoctl(struct NaClAppThread *natp,
                     int                  d,
                     int                  request,
                     void                 *arg) {
  return NaClCommonSysIoctl(natp, d, request, arg);
}

int32_t NaClSysFstat(struct NaClAppThread *natp,
                     int                  d,
                     struct nacl_abi_stat *nasp) {
  return NaClCommonSysFstat(natp, d, nasp);
}

int32_t NaClSysStat(struct NaClAppThread *natp,
                    const char           *path,
                    struct nacl_abi_stat *nasp) {
  return NaClCommonSysStat(natp, path, nasp);
}

int32_t NaClSysGetdents(struct NaClAppThread  *natp,
                        int                   d,
                        void                  *buf,
                        size_t                count) {
  return NaClCommonSysGetdents(natp, d, buf, count);
}

int32_t NaClSysSysbrk(struct NaClAppThread  *natp,
                      void                  *new_break) {
  return NaClSetBreak(natp, (uintptr_t) new_break);
}

int32_t NaClSysMmap(struct NaClAppThread  *natp,
                    void                  *start,
                    size_t                length,
                    int                   prot,
                    int                   flags,
                    int                   d,
                    nacl_abi_off_t        offset) {
  return NaClCommonSysMmap(natp, start, length, prot, flags, d, offset);
}

int32_t NaClSysMunmap(struct NaClAppThread  *natp,
                      void                  *start,
                      size_t                length) {
  int32_t   retval = -NACL_ABI_EINVAL;
  uintptr_t sysaddr;
  int       holding_app_lock = 0;
  size_t    alloc_rounded_length;

  NaClLog(3, "NaClSysMunmap(0x%08"PRIxPTR", 0x%08"PRIxPTR", 0x%"PRIxS")\n",
          (uintptr_t) natp, (uintptr_t) start, length);

  NaClSysCommonThreadSyscallEnter(natp);

  if (!NaClIsAllocPageMultiple((uintptr_t) start)) {
    NaClLog(4, "start addr not allocation multiple\n");
    retval = -NACL_ABI_EINVAL;
    goto cleanup;
  }
  if (0 == length) {
    /*
     * linux mmap of zero length yields a failure, but osx does not, leading
     * to a NaClVmmapUpdate of zero pages, which should not occur.
     */
    retval = -NACL_ABI_EINVAL;
    goto cleanup;
  }
  alloc_rounded_length = NaClRoundAllocPage(length);
  if (alloc_rounded_length != length) {
    length = alloc_rounded_length;
    NaClLog(LOG_WARNING,
            "munmap: rounded length to 0x%"PRIxS"\n", length);
  }
  sysaddr = NaClUserToSysAddrRange(natp->nap, (uintptr_t) start, length);
  if (kNaClBadAddress == sysaddr) {
    NaClLog(4, "region not user addresses\n");
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }

  NaClXMutexLock(&natp->nap->mu);
  holding_app_lock = 1;
  /*
   * NB: windows (or generic) version would use Munmap virtual
   * function from the backing NaClDesc object obtained by iterating
   * through the address map for the region, and those Munmap virtual
   * functions may return -NACL_ABI_E_MOVE_ADDRESS_SPACE.
   *
   * We should hold the application lock while doing this iteration
   * and unmapping, so that the address space is consistent for other
   * threads.
   */

  /*
   * User should be unable to unmap any executable pages.  We check here.
   */
  if (NaClSysCommonAddrRangeContainsExecutablePages_mu(natp->nap,
                                                       (uintptr_t) start,
                                                       length)) {
    NaClLog(2, "NaClSysMunmap: region contains executable pages\n");
    retval = -NACL_ABI_EINVAL;
    goto cleanup;
  }

  /*
   * Overwrite current mapping with inaccessible, anonymous
   * zero-filled pages, which should be copy-on-write and thus
   * relatively cheap.  Do not open up an address space hole.
   */
  NaClLog(3,
          ("NaClSysMunmap: mmap(0x%08"PRIxPTR", 0x%"PRIxS","
           " 0x%x, 0x%x, -1, 0)\n"),
          sysaddr, length, PROT_NONE,
          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED);
  if (MAP_FAILED == mmap((void *) sysaddr,
                         length,
                         PROT_NONE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                         -1,
                         (off_t) 0)) {
    NaClLog(3, "mmap to put in anonymous memory failed, errno = %d\n", errno);
    retval = -NaClXlateErrno(errno);
    goto cleanup;
  }
  NaClVmmapUpdate(&natp->nap->mem_map,
                  (NaClSysToUser(natp->nap, (uintptr_t) sysaddr)
                   >> NACL_PAGESHIFT),
                  length >> NACL_PAGESHIFT,
                  0,  /* prot */
                  (struct NaClMemObj *) NULL,
                  1);  /* Delete mapping */
  retval = 0;
cleanup:
  if (holding_app_lock) {
    NaClXMutexUnlock(&natp->nap->mu);
  }
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

int32_t NaClSysExit(struct NaClAppThread *natp,
                    int                  status) {
  return NaClCommonSysExit(natp, status);
}

int32_t NaClSysGetpid(struct NaClAppThread *natp) {
  int32_t pid;

  NaClSysCommonThreadSyscallEnter(natp);

  pid = getpid();  /* TODO(bsy): obfuscate? */
  NaClLog(4, "NaClSysGetpid: returning %d\n", pid);

  NaClSysCommonThreadSyscallLeave(natp);
  return pid;
}

int32_t NaClSysThread_Exit(struct NaClAppThread  *natp,
                           int32_t               *stack_flag) {
  return NaClCommonSysThreadExit(natp, stack_flag);
}

int32_t NaClSysGetTimeOfDay(struct NaClAppThread      *natp,
                            struct nacl_abi_timeval   *tv,
                            struct nacl_abi_timezone  *tz) {
  uintptr_t               sysaddr;
  int                     retval;
  struct nacl_abi_timeval now;

  UNREFERENCED_PARAMETER(tz);
  NaClSysCommonThreadSyscallEnter(natp);

  sysaddr = NaClUserToSysAddrRange(natp->nap, (uintptr_t) tv, sizeof tv);

  /*
   * tz is not supported in linux, nor is it supported by glibc, since
   * tzset(3) and the zoneinfo file should be used instead.
   *
   * TODO(bsy) Do we make the zoneinfo directory available to
   * applications?
   */

  if (kNaClBadAddress == sysaddr) {
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }

  retval = NaClGetTimeOfDay(&now);
  if (0 == retval) {
    /*
     * To make it harder to distinguish Linux platforms from Windows,
     * coarsen the time to the same level we get on Windows -
     * milliseconds, unless in "debug" mode.
     */
    if (!NaClHighResolutionTimerEnabled()) {
      now.nacl_abi_tv_usec = (now.nacl_abi_tv_usec / 1000) * 1000;
    }
    *(struct nacl_abi_timeval *) sysaddr = now;
  }
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

/*
 * TODO(bsy): REMOVE THIS AND PROVIDE GETRUSAGE.  This is normally
 * not a syscall; instead, it is a library routine on top of
 * getrusage, with appropriate clock tick translation.
 */
int32_t NaClSysClock(struct NaClAppThread *natp) {
  int32_t retval;

  NaClSysCommonThreadSyscallEnter(natp);
  retval = clock();
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

#if defined(HAVE_SDL)

int32_t NaClSysMultimedia_Init(struct NaClAppThread *natp,
                               int                  subsys) {
  /* tail call, should compile to a jump */
  return NaClCommonSysMultimedia_Init(natp, subsys);
}

int32_t NaClSysMultimedia_Shutdown(struct NaClAppThread *natp) {
  return NaClCommonSysMultimedia_Shutdown(natp);
}

int32_t NaClSysVideo_Init(struct NaClAppThread *natp,
                          int                  width,
                          int                  height) {
  /* tail call, should compile to a jump */
  return NaClCommonSysVideo_Init(natp, width, height);
}

int32_t NaClSysVideo_Shutdown(struct NaClAppThread *natp) {
  return NaClCommonSysVideo_Shutdown(natp);
}

int32_t NaClSysVideo_Update(struct NaClAppThread *natp,
                            unsigned char        *data) {
  /* tail call, should compile to a jump */
  return NaClCommonSysVideo_Update(natp, data);
}

int32_t NaClSysVideo_Poll_Event(struct NaClAppThread *natp,
                                union NaClMultimediaEvent *event) {
  return NaClCommonSysVideo_Poll_Event(natp, event);
}

int32_t NaClSysAudio_Init(struct NaClAppThread *natp,
                          enum NaClAudioFormat format,
                          int                  desired_samples,
                          int                  *obtained_samples) {
  return NaClCommonSysAudio_Init(natp, format,
                                 desired_samples, obtained_samples);
}


int32_t NaClSysAudio_Stream(struct NaClAppThread *natp,
                            const void           *data,
                            size_t               *size) {
  return NaClSliceSysAudio_Stream(natp, data, size);
}


int32_t NaClSysAudio_Shutdown(struct NaClAppThread *natp) {
  return NaClCommonSysAudio_Shutdown(natp);
}


#endif /* HAVE_SDL */

int32_t NaClSysSrpc_Get_Fd(struct NaClAppThread *natp) {
  extern int NaClSrpcFileDescriptor;

  UNREFERENCED_PARAMETER(natp);
  return NaClSrpcFileDescriptor;
}

int32_t NaClSysImc_MakeBoundSock(struct NaClAppThread *natp,
                                 int                  *sap) {
  return NaClCommonSysImc_MakeBoundSock(natp, sap);
}

int32_t NaClSysImc_Accept(struct NaClAppThread  *natp,
                          int                   d) {
  return NaClCommonSysImc_Accept(natp, d);
}

int32_t NaClSysImc_Connect(struct NaClAppThread *natp,
                           int                  d) {
  return NaClCommonSysImc_Connect(natp, d);
}

int32_t NaClSysImc_Sendmsg(struct NaClAppThread *natp,
                       int                  d,
                       struct NaClImcMsgHdr *nimhp,
                       int                  flags) {
  return NaClCommonSysImc_Sendmsg(natp, d, nimhp, flags);
}

int32_t NaClSysImc_Recvmsg(struct NaClAppThread *natp,
                           int                  d,
                           struct NaClImcMsgHdr *nimhp,
                           int                  flags) {
  return NaClCommonSysImc_Recvmsg(natp, d, nimhp, flags);
}

int32_t NaClSysImc_Mem_Obj_Create(struct NaClAppThread  *natp,
                                  size_t                size) {
  return NaClCommonSysImc_Mem_Obj_Create(natp, size);
}

int32_t NaClSysTls_Init(struct NaClAppThread  *natp,
                        void                  *tdb,
                        size_t                size) {
  return NaClCommonSysTls_Init(natp, tdb, size);
}

int32_t NaClSysThread_Create(struct NaClAppThread *natp,
                             void                 *prog_ctr,
                             void                 *stack_ptr,
                             void                 *tdb,
                             size_t               tdb_size) {
  return NaClCommonSysThread_Create(natp, prog_ctr, stack_ptr, tdb, tdb_size);
}

int32_t NaClSysThread_Nice(struct NaClAppThread *natp, const int nice) {
  return NaClCommonSysThread_Nice(natp, nice);
}

/* mutex */

int32_t NaClSysMutex_Create(struct NaClAppThread *natp) {
  return NaClCommonSysMutex_Create(natp);
}

int32_t NaClSysMutex_Lock(struct NaClAppThread *natp,
                          int32_t              mutex_handle) {
  return NaClCommonSysMutex_Lock(natp, mutex_handle);
}

int32_t NaClSysMutex_Unlock(struct NaClAppThread *natp,
                            int32_t              mutex_handle) {
  return NaClCommonSysMutex_Unlock(natp, mutex_handle);
}

int32_t NaClSysMutex_Trylock(struct NaClAppThread *natp,
                             int32_t              mutex_handle) {
  return NaClCommonSysMutex_Trylock(natp, mutex_handle);
}


/* condition variable */

int32_t NaClSysCond_Create(struct NaClAppThread *natp) {
  return NaClCommonSysCond_Create(natp);
}

int32_t NaClSysCond_Wait(struct NaClAppThread *natp,
                         int32_t              cond_handle,
                         int32_t              mutex_handle) {
  return NaClCommonSysCond_Wait(natp, cond_handle, mutex_handle);
}

int32_t NaClSysCond_Signal(struct NaClAppThread *natp,
                           int32_t              cond_handle) {
  return NaClCommonSysCond_Signal(natp, cond_handle);
}

int32_t NaClSysCond_Broadcast(struct NaClAppThread *natp,
                              int32_t              cond_handle) {
  return NaClCommonSysCond_Broadcast(natp, cond_handle);
}

int32_t NaClSysCond_Timed_Wait_Abs(struct NaClAppThread     *natp,
                                   int32_t                  cond_handle,
                                   int32_t                  mutex_handle,
                                   struct nacl_abi_timespec *ts) {
  return NaClCommonSysCond_Timed_Wait_Abs(natp,
                                          cond_handle,
                                          mutex_handle,
                                          ts);
}

int32_t NaClSysImc_SocketPair(struct NaClAppThread  *natp,
                              int32_t               *d_out) {
  return NaClCommonSysImc_SocketPair(natp, d_out);
}
int32_t NaClSysSem_Create(struct NaClAppThread *natp,
                          int32_t              init_value) {
  return NaClCommonSysSem_Create(natp, init_value);
}

int32_t NaClSysSem_Wait(struct NaClAppThread *natp,
                        int32_t              sem_handle) {
  return NaClCommonSysSem_Wait(natp, sem_handle);
}

int32_t NaClSysSem_Post(struct NaClAppThread *natp,
                        int32_t              sem_handle) {
  return NaClCommonSysSem_Post(natp, sem_handle);
}

int32_t NaClSysSem_Get_Value(struct NaClAppThread *natp,
                             int32_t              sem_handle) {
  return NaClCommonSysSem_Get_Value(natp, sem_handle);
}

int32_t NaClSysSched_Yield(struct NaClAppThread *natp) {
  UNREFERENCED_PARAMETER(natp);
  sched_yield();
  return 0;
}

int32_t NaClSysSysconf(struct NaClAppThread *natp,
                       int32_t name,
                       int32_t *result) {
  int32_t         retval = -NACL_ABI_EINVAL;
  static int32_t  number_of_workers = -1;
  uintptr_t       sysaddr;

  NaClSysCommonThreadSyscallEnter(natp);

  sysaddr = NaClUserToSysAddrRange(natp->nap,
                                   (uintptr_t) result,
                                   sizeof(*result));
  if (kNaClBadAddress == sysaddr) {
    retval = -NACL_ABI_EINVAL;
    goto cleanup;
  }

  switch (name) {
#ifdef _SC_NPROCESSORS_ONLN
    case NACL_ABI__SC_NPROCESSORS_ONLN: {
      if (-1 == number_of_workers) {
        number_of_workers = sysconf(_SC_NPROCESSORS_ONLN);
      }
      if (-1 == number_of_workers) {
        /* failed to get the number of processors */
        retval = -NACL_ABI_EINVAL;
        goto cleanup;
      }
      *(int32_t*)sysaddr = number_of_workers;
      break;
    }
#endif
    default:
      retval = -NACL_ABI_EINVAL;
      goto cleanup;
  }
  retval = 0;
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

//@author: nizam following are the wrappers around socket lib functions
//that are outside the sandbox
int32_t NaClSysAccept(struct NaClAppThread  *natp, int fd, struct sockaddr* addr,
		   socklen_t *addr_len) {
	int r;
	if ((r = NaClValidateIp(natp, addr)) != 0) {
	  return r;
	}
	return accept(fd, addr, addr_len);
}

int32_t NaClSysBind(struct NaClAppThread  *natp, int fd,
		const struct sockaddr * addr, socklen_t len) {
	int r;
        struct sockaddr *newaddr = (struct sockaddr *)NaClUserToSysAddrRange(natp->nap, (uintptr_t) addr, len);
	if (kNaClBadAddress == (uintptr_t)addr) {
	  return -NACL_ABI_EFAULT;	 
	}
	if ((r = NaClValidateIp(natp, newaddr)) != 0) {
	  return r;
	}
	return bind(fd, newaddr, len);
}
 
int32_t NaClSysConnect(struct NaClAppThread  *natp, int fd,
		const struct sockaddr* addr, socklen_t len) {
	int r;

	struct sockaddr_in *addr_in = (struct sockaddr_in *)NaClUserToSysAddrRange(natp->nap, (uintptr_t) addr, len); /* Echo server address */

	if (kNaClBadAddress == (uintptr_t)addr_in) {
	  return -NACL_ABI_EFAULT;	 
	}

	if ((r = NaClValidateIp(natp, (struct sockaddr *)addr_in)) != 0) {
	  return r;
	}

	r = connect(fd, (struct sockaddr *)addr_in, len);

	return r;
}

int32_t NaClSysGetpeername(struct NaClAppThread  *natp, int fd,
		struct sockaddr* addr, socklen_t *len) {
	int r;
	if ((r = NaClValidateIp(natp, addr)) != 0) {
	  return r;
	}
	return getpeername(fd, addr, len);
}

int32_t NaClSysGetsockname(struct NaClAppThread  *natp, int fd,
		struct sockaddr* addr, socklen_t *len) {
	int r;
	if ((r = NaClValidateIp(natp, addr)) != 0) {
	  return r;
	}
	return getsockname(fd, addr, len);
}

int32_t NaClSysGetsockopt(struct NaClAppThread  *natp, int fd, int level,
		   int optname,
	       void *optval,
	       socklen_t *optlen) {
	UNREFERENCED_PARAMETER(natp);
	return getsockopt(fd, level, optname, optval, optlen);
}

int32_t NaClSysListen(struct NaClAppThread  *natp, int fd, int n) {
	UNREFERENCED_PARAMETER(natp);
	return listen(fd, n);
}

int32_t NaClSysRecv(struct NaClAppThread  *natp, int fd, void *buf,
		size_t n, int flags) {
        void *newbuf = (void *)NaClUserToSysAddrRange(natp->nap, (uintptr_t) buf, n);
	if (kNaClBadAddress == (uintptr_t)newbuf) {
	  return -NACL_ABI_EFAULT;	 
	}
	return recv(fd, newbuf, n, flags);
}

int32_t NaClSysRecvfrom(struct NaClAppThread  *natp, int fd, void *buf, size_t n,
		 int flags, struct sockaddr* addr,
		 socklen_t *addr_len) {
	int r;
	if ((r = NaClValidateIp(natp, addr)) != 0) {
	  return r;
	}
	return recvfrom(fd, buf, n, flags, addr, addr_len);
}

int32_t NaClSysRecvmsg(struct NaClAppThread  *natp, int fd,
		struct msghdr *message, int flags) {
        struct msghdr *newmessage = (void *)NaClUserToSysAddrRange(natp->nap, (uintptr_t) message, sizeof(struct msghdr));
	if (kNaClBadAddress == (uintptr_t)newmessage) {
	  return -NACL_ABI_EFAULT;	 
	}
	return recvmsg(fd, newmessage, flags);
}

int32_t NaClSysSend(struct NaClAppThread  *natp, int fd,
		const void *buf, size_t n, int flags) {
        void *newbuf = (void *)NaClUserToSysAddrRange(natp->nap, (uintptr_t) buf, n);
	if (kNaClBadAddress == (uintptr_t)newbuf) {
	  return -NACL_ABI_EFAULT;	 
	}
	return send(fd, newbuf, n, flags);
}

int32_t NaClSysSendMsg(struct NaClAppThread  *natp, int fd,
		const struct msghdr *message,
		int flags) {
        void *newmessage = (void *)NaClUserToSysAddrRange(natp->nap, (uintptr_t) message, sizeof(struct msghdr));
	if (kNaClBadAddress == (uintptr_t)newmessage) {
	  return -NACL_ABI_EFAULT;	 
	}
	return sendmsg(fd, newmessage, flags);
}

int32_t NaClSysSendto(struct NaClAppThread  *natp, int fd,
		   const void *buf, size_t n,
	       int flags, const struct sockaddr* addr,
	       socklen_t addr_len) {
	int r;
        void *newbuf = (void *)NaClUserToSysAddrRange(natp->nap, (uintptr_t) buf, n);
	if (kNaClBadAddress == (uintptr_t)newbuf) {
	  return -NACL_ABI_EFAULT;	 
	}
	if ((r = NaClValidateIp(natp, addr)) != 0) {
	  return r;
	}
	return sendto(fd, newbuf, n, flags, addr, addr_len);
}

int32_t NaClSysSetsockopt(struct NaClAppThread  *natp, int fd, int level,
		int optname, const void *optval, socklen_t optlen) {
        void *newoptval = (void *)NaClUserToSysAddrRange(natp->nap, (uintptr_t) optval, optlen);
	if (kNaClBadAddress == (uintptr_t)newoptval) {
	  return -NACL_ABI_EFAULT;	 
	}
	return setsockopt(fd, level, optname, newoptval, optlen);
}

int32_t NaClSysShutdown(struct NaClAppThread  *natp, int fd, int how) {
	UNREFERENCED_PARAMETER(natp);
	return shutdown(fd, how);
}

int32_t NaClSysSocket(struct NaClAppThread  *natp, int domain, int type,
		int protocol) {
	UNREFERENCED_PARAMETER(natp);
	return socket(domain, type, protocol);
}

int32_t NaClSysSocketpair(struct NaClAppThread  *natp, int domain, int type,
		int protocol, int fds[2]) {
	UNREFERENCED_PARAMETER(natp);
	return socketpair(domain, type, protocol, fds);
}
