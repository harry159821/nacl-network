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

/*
 * NaCl service run-time, non-platform specific system call helper routines.
 */
#include "native_client/src/include/portability_string.h"
#include "native_client/src/include/nacl_platform.h"
#include "native_client/src/include/nacl_macros.h"

#include "native_client/src/shared/platform/nacl_host_desc.h"
#include "native_client/src/shared/platform/nacl_host_dir.h"
#include "native_client/src/shared/platform/nacl_sync_checked.h"

#include "native_client/src/trusted/desc/nacl_desc_base.h"
#include "native_client/src/trusted/desc/nacl_desc_cond.h"
#include "native_client/src/trusted/desc/nacl_desc_dir.h"
#include "native_client/src/trusted/desc/nacl_desc_io.h"
#include "native_client/src/trusted/desc/nacl_desc_imc.h"
#include "native_client/src/trusted/desc/nacl_desc_imc_shm.h"
#include "native_client/src/trusted/desc/nacl_desc_mutex.h"
#include "native_client/src/trusted/desc/nacl_desc_semaphore.h"
#include "native_client/src/trusted/desc/nrd_xfer.h"

#include "native_client/src/trusted/service_runtime/include/sys/errno.h"
#include "native_client/src/trusted/service_runtime/nacl_app_thread.h"
#include "native_client/src/trusted/service_runtime/nacl_thread_nice.h"
#include "native_client/src/trusted/service_runtime/nacl_globals.h"
#include "native_client/src/trusted/service_runtime/nacl_tls.h"
#include "native_client/src/trusted/service_runtime/sel_ldr.h"
#include "native_client/src/trusted/service_runtime/sel_memory.h"

#include "native_client/src/trusted/service_runtime/include/sys/errno.h"
#include "native_client/src/trusted/service_runtime/include/sys/fcntl.h"
#include "native_client/src/trusted/service_runtime/include/sys/mman.h"
#include "native_client/src/trusted/service_runtime/include/sys/stat.h"


static INLINE size_t  size_min(size_t a, size_t b) {
  return (a < b) ? a : b;
}

/*
 * natp should be thread_self(), called while holding no locks.
 */
void NaClSysCommonThreadSuicide(struct NaClAppThread  *natp) {
  struct NaClApp  *nap;
  uint16_t        thread_idx;

  /*
   * mark this thread as dead; doesn't matter if some other thread is
   * asking us to commit suicide.
   */
  NaClLog(3, "NaClSysCommonThreadSuicide(0x%08"PRIxPTR")\n", (uintptr_t) natp);
  nap = natp->nap;
  NaClLog(3, " getting thread table lock\n");
  NaClXMutexLock(&nap->threads_mu);
  NaClLog(3, " getting thread lock\n");
  NaClXMutexLock(&natp->mu);
  natp->state = NACL_APP_THREAD_DEAD;
  /*
   * Remove ourselves from the ldt-indexed global tables.  The ldt
   * entry is released as part of NaClAppThreadDtor (via
   * NaClAppThreadDecRef), and if another thread is immediately
   * created (from some other running thread) we want to be sure that
   * any ldt-based lookups will not reach this dying thread's data.
   */
  thread_idx = NaClGetThreadIdx(natp);
  nacl_sys[thread_idx] = NULL;
  nacl_user[thread_idx] = NULL;
  nacl_thread[thread_idx] = NULL;
  NaClLog(3, " removing thread from thread table\n");
  NaClRemoveThreadMu(nap, natp->thread_num);
  NaClLog(3, " unlocking thread\n");
  NaClXMutexUnlock(&natp->mu);
  NaClLog(3, " announcing thread count change\n");
  NaClXCondVarBroadcast(&nap->threads_cv);
  NaClLog(3, " unlocking thread table\n");
  NaClXMutexUnlock(&nap->threads_mu);
  NaClLog(3, " decref'ing thread object (from count %d)\n", natp->refcount);
  NaClAppThreadDecRef(natp);
  NaClLog(3, " NaClThreadExit\n");
  NaClThreadExit();  /* should not return */
  NaClLog(LOG_ERROR, "INCONCEIVABLE!\n");
  abort();
  /* NOTREACHED */
}

void NaClSysCommonThreadSyscallEnter(struct NaClAppThread *natp) {
  NaClLog(4, "NaClSysCommonThreadSyscallEnter: locking 0x%08"PRIxPTR"\n",
          (uintptr_t) &natp->mu);
  NaClXMutexLock(&natp->mu);
  natp->holding_sr_locks = 1;
  NaClLog(4, "NaClSysCommonThreadSyscallEnter: unlocking 0x%08"PRIxPTR"\n\n",
          (uintptr_t) &natp->mu);
  NaClXMutexUnlock(&natp->mu);
}

void NaClSysCommonThreadSyscallLeave(struct NaClAppThread *natp) {
  NaClXMutexLock(&natp->mu);
  natp->holding_sr_locks = 0;
  switch (natp->state) {
    case NACL_APP_THREAD_ALIVE:
      break;
    case NACL_APP_THREAD_SUICIDE_PENDING:
      NaClXMutexUnlock(&natp->mu);
      NaClSysCommonThreadSuicide(natp);
      /* NOTREACHED */
      break;
    case NACL_APP_THREAD_DEAD:
      NaClLog(LOG_FATAL, "Dead thread at NaClSysCommonThreadSyscallLeave\n");
      /* NOTREACHED */
      break;
  }
  NaClXMutexUnlock(&natp->mu);
}

int32_t NaClSetBreak(struct NaClAppThread *natp,
                     uintptr_t            new_break) {
  struct NaClApp        *nap;
  int32_t               rv;
  struct NaClVmmapIter  iter;
  struct NaClVmmapEntry *ent;
  struct NaClVmmapEntry *next_ent;
  uintptr_t             sys_break;
  uintptr_t             sys_new_break;
  uintptr_t             usr_last_data_page;
  uintptr_t             usr_new_last_data_page;
  uintptr_t             last_internal_data_addr;
  uintptr_t             last_internal_page;
  uintptr_t             start_new_region;
  uintptr_t             region_size;

  nap = natp->nap;
  rv = nap->break_addr;

  NaClLog(2, "NaClSetBreak: new_break 0x%08"PRIxPTR"\n", new_break);

  NaClSysCommonThreadSyscallEnter(natp);

  sys_new_break = NaClUserToSysAddr(natp->nap, new_break);
  NaClLog(2, "sys_new_break 0x%08"PRIxPTR"\n", sys_new_break);

  if (kNaClBadAddress == sys_new_break) {
    goto cleanup_no_lock;
  }
  if (NACL_SYNC_OK != NaClMutexLock(&nap->mu)) {
    NaClLog(LOG_ERROR, "Could not get app lock for 0x%08"PRIxPTR"\n",
            (uintptr_t) nap);
    goto cleanup_no_lock;
  }
  if (new_break < nap->data_end) {
    goto cleanup;
  }
  if (new_break <= nap->break_addr) {
    /* freeing memory */
    nap->break_addr = new_break;
  } else {

    /*
     * See if page containing new_break is in mem_map; if so, we are
     * essentially done -- just update break_addr.  Otherwise, we
     * extend the VM map entry from the page containing the current
     * break to the page containing new_break.
     */

    sys_break = NaClUserToSys(nap, nap->break_addr);

    usr_last_data_page = (nap->break_addr - 1) >> NACL_PAGESHIFT;

    usr_new_last_data_page = (new_break - 1) >> NACL_PAGESHIFT;

    last_internal_data_addr = NaClRoundAllocPage(new_break) - 1;
    last_internal_page = last_internal_data_addr >> NACL_PAGESHIFT;

    NaClLog(2, ("current break sys addr 0x%08"PRIxPTR
                ", usr last data page 0x%"PRIxPTR"\n"),
            sys_break, usr_last_data_page);
    NaClLog(2, "new break usr last data page 0x%"PRIxPTR"\n",
            usr_new_last_data_page);
    NaClLog(3, "last internal data addr 0x%08"PRIxPTR"\n",
            last_internal_data_addr);

    if (NULL == NaClVmmapFindPageIter(&nap->mem_map,
                                      usr_last_data_page,
                                      &iter)
        || NaClVmmapIterAtEnd(&iter)) {
      NaClLog(LOG_FATAL, ("current break (0x%08"PRIxPTR", sys 0x%08"PRIxPTR")"
                          " not in address map\n"),
              nap->break_addr, sys_break);
    }
    ent = NaClVmmapIterStar(&iter);
    NaClLog(2, ("segment containing current break"
                ": page_num 0x%08"PRIxPTR", npages 0x%"PRIxS"\n"),
            ent->page_num, ent->npages);
    if (usr_new_last_data_page < ent->page_num + ent->npages) {
      NaClLog(2, "new break within break segment, just bumping addr\n");
      nap->break_addr = new_break;
      rv = new_break;
    } else {
      NaClVmmapIterIncr(&iter);
      if (!NaClVmmapIterAtEnd(&iter)
          && ((next_ent = NaClVmmapIterStar(&iter))->page_num
              <= last_internal_page)) {
        /* ran into next segment! */
        NaClLog(1,
                ("new break request of usr address"
                 " 0x%08"PRIxPTR" / usr page 0x%"PRIxPTR
                 " runs into next region, page_num 0x%"PRIxPTR
                 ", npages 0x%"PRIxS"\n"),
                new_break, usr_new_last_data_page,
                next_ent->page_num, next_ent->npages);
        goto cleanup;
      }
      NaClLog(2,
              "extending segment: page_num 0x%08"PRIxPTR", npages 0x%"PRIxS"\n",
              ent->page_num, ent->npages);
      /* go ahead and extend ent to cover, and make pages accessible */
      start_new_region = (ent->page_num + ent->npages) << NACL_PAGESHIFT;
      ent->npages = (last_internal_page - ent->page_num + 1);
      region_size = (((last_internal_page + 1) << NACL_PAGESHIFT)
                     - start_new_region);
      if (0 != NaCl_mprotect((void *) NaClUserToSys(nap, start_new_region),
                             region_size,
                             PROT_READ | PROT_WRITE)) {
        NaClLog(LOG_FATAL,
                ("Could not mprotect(0x%08"PRIxPTR", 0x%08"PRIxPTR", "
                 "PROT_READ|PROT_WRITE)\n"),
                start_new_region,
                region_size);
      }
      NaClLog(2, "segment now: page_num 0x%08"PRIxPTR", npages 0x%"PRIxS"\n",
              ent->page_num, ent->npages);
      nap->break_addr = new_break;
      rv = new_break;
    }
  }

cleanup:
  (void) NaClMutexUnlock(&nap->mu);
cleanup_no_lock:
  NaClSysCommonThreadSyscallLeave(natp);

  NaClLog(2, "NaClSetBreak: returning 0x%08"PRIx32"\n", rv);
  return rv;
}

static int NaClAclBypassChecks = 0;

void NaClInsecurelyBypassAllAclChecks(void) {
  NaClLog(LOG_WARNING, "BYPASSING ALL ACL CHECKS\n");
  NaClAclBypassChecks = 1;
}

int NaClHighResolutionTimerEnabled() {
  return NaClAclBypassChecks;
}

/*
 * NaClOpenAclCheck: Is the NaCl app authorized to open this file?  The
 * return value is syscall return convention, so 0 is success and
 * small negative numbers are negated errno values.
 */
int32_t NaClOpenAclCheck(struct NaClApp *nap,
                         char const     *path,
                         int            flags,
                         int            mode) {
  /*
   * TODO(bsy): provide some minimal authorization check, based on
   * whether a debug flag is set; eventually provide a data-driven
   * authorization configuration mechanism, perhaps persisted via
   * gears.  need GUI for user configuration, as well as designing an
   * appropriate language (with sufficient expressiveness), however.
   */
  NaClLog(LOG_INFO, "NaClOpenAclCheck(0x%08"PRIxPTR", %s, 0%o, 0%o)\n",
          (uintptr_t) nap, path, flags, mode);
  if (3 < NaClLogGetVerbosity()) {
    NaClLog(0, "O_ACCMODE: 0%o\n", flags & NACL_ABI_O_ACCMODE);
    NaClLog(0, "O_RDONLY = %d\n", NACL_ABI_O_RDONLY);
    NaClLog(0, "O_WRONLY = %d\n", NACL_ABI_O_WRONLY);
    NaClLog(0, "O_RDWR   = %d\n", NACL_ABI_O_RDWR);
#define FLOG(VAR, BIT) do {\
      NaClLog(LOG_INFO, "%s: %s\n", #BIT, (VAR & BIT) ? "yes" : "no");\
    } while (0)
    FLOG(flags, NACL_ABI_O_CREAT);
    FLOG(flags, NACL_ABI_O_TRUNC);
    FLOG(flags, NACL_ABI_O_APPEND);
#undef FLOG
  }
  if (NaClAclBypassChecks) {
    return 0;
  }
  return -NACL_ABI_EACCES;
}

/*
 * NaClStatAclCheck: Is the NaCl app authorized to stat this pathname?  The
 * return value is syscall return convention, so 0 is success and
 * small negative numbers are negated errno values.
 */
int32_t NaClStatAclCheck(struct NaClApp *nap,
                         char const     *path) {
  /*
   * TODO(bsy): provide some minimal authorization check, based on
   * whether a debug flag is set; eventually provide a data-driven
   * authorization configuration mechanism, perhaps persisted via
   * gears.  need GUI for user configuration, as well as designing an
   * appropriate language (with sufficient expressiveness), however.
   */
  NaClLog(LOG_INFO,
          "NaClStatAclCheck(0x%08"PRIxPTR", %s)\n", (uintptr_t) nap, path);
  if (NaClAclBypassChecks) {
    return 0;
  }
  return -NACL_ABI_EACCES;
}

int32_t NaClIoctlAclCheck(struct NaClApp  *nap,
                          struct NaClDesc *ndp,
                          int             request,
                          void            *arg) {
  NaClLog(LOG_INFO,
          ("NaClIoctlAclCheck(0x%08"PRIxPTR", 0x%08"PRIxPTR","
           " %d, 0x%08"PRIxPTR"\n"),
          (uintptr_t) nap, (uintptr_t) ndp, request, (uintptr_t) arg);
  if (NaClAclBypassChecks) {
    return 0;
  }
  return -NACL_ABI_EINVAL;
}

int32_t NaClCommonSysExit(struct NaClAppThread  *natp,
                          int                   status) {
  struct NaClApp            *nap;

  NaClLog(LOG_INFO, "Exit syscall handler: %d\n", status);
  NaClSysCommonThreadSyscallEnter(natp);

  nap = natp->nap;
  NaClSyncQueueQuit(&nap->work_queue);

  NaClXMutexLock(&nap->mu);
  nap->exit_status = status;
  nap->running = 0;
  NaClCondVarSignal(&nap->cv);
  NaClXMutexUnlock(&nap->mu);

  NaClSysCommonThreadSuicide(natp);
  /* NOTREACHED */
  return -NACL_ABI_EINVAL;
}

int32_t NaClCommonSysThreadExit(struct NaClAppThread  *natp,
                                int32_t               *stack_flag) {
  uintptr_t sys_stack_flag;
  struct NaClApp  *nap;

  NaClLog(4, "NaclCommonSysThreadExit(0x%08"PRIxPTR", 0x%08"PRIxPTR"\n",
          (uintptr_t) natp,
          (uintptr_t) stack_flag);
  NaClSysCommonThreadSyscallEnter(natp);
  /*
   * NB: NaClThreads are never joinable, but the abstraction for NaClApps
   * are.
   */
  nap = natp->nap;

  if (NULL != stack_flag) {
    sys_stack_flag = NaClUserToSys(natp->nap, (uintptr_t) stack_flag);
    if (kNaClBadAddress != sys_stack_flag) {
      /*
       * We don't return failure if the address is illegal because
       * this function is not supposed to return.
       */
      *(int32_t *) sys_stack_flag = 0;
    }
  }

  NaClSysCommonThreadSuicide(natp);
  /* NOTREACHED */
  return -NACL_ABI_EINVAL;
}

int32_t NaClCommonSysOpen(struct NaClAppThread  *natp,
                          char                  *pathname,
                          int                   flags,
                          int                   mode) {
  uint32_t             retval = -NACL_ABI_EINVAL;
  uintptr_t            sysaddr;
  char                 path[NACL_CONFIG_PATH_MAX];
  int                  len;
  struct nacl_abi_stat stbuf;
  int                  allowed_flags;

  NaClLog(3, "NaClCommonSysOpen(0x%08"PRIxPTR", 0x%08"PRIxPTR", 0x%x, 0x%x)\n",
          (uintptr_t) natp, (uintptr_t) pathname, flags, mode);

  NaClSysCommonThreadSyscallEnter(natp);

  sysaddr = NaClUserToSysAddr(natp->nap, (uintptr_t) pathname);
  if (kNaClBadAddress == sysaddr) {
    NaClLog(LOG_ERROR, "Invalid address for pathname\n");
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }
  allowed_flags = (NACL_ABI_O_ACCMODE | NACL_ABI_O_CREAT
                   | NACL_ABI_O_TRUNC | NACL_ABI_O_APPEND);
  if (0 != (flags & ~allowed_flags)) {
    NaClLog(LOG_WARNING, "Invalid open flags 0%o, ignoring extraneous bits\n",
            flags);
    flags &= allowed_flags;
  }
  if (0 != (mode & ~0600)) {
    NaClLog(LOG_INFO, "IGNORING Invalid access mode bits 0%o\n", mode);
    mode &= 0600;
  }
  NaClLog(4, " attempting to copy path via sysaddr 0x%08"PRIxPTR"\n", sysaddr);
  NaClLog(4, " first 4 bytes: %.4s\n", (char *) sysaddr);
  /*
   * strncpy may (try to) get bytes that is outside the app's address
   * space and generate a fault.
   */
  strncpy(path, (char *) sysaddr, sizeof path);
  /*
   * survived the copy, but did there happen to be data beyond the end?
   */
  path[sizeof path - 1] = '\0';  /* always null terminate */
  NaClLog(LOG_INFO, "NaClSysOpen: Path: %s\n", path);
  len = strlen(path);
  /*
   * make sure sysaddr is a string, and the whole string is in app
   * address space...
   *
   * address space is convex, so it is impossible for beginning and
   * end to be both in the address space and yet have an intermediate
   * byte not be in the address space.
   */
  if (kNaClBadAddress == NaClUserToSysAddr(natp->nap,
                                           len + (uintptr_t) pathname)) {
    NaClLog(LOG_ERROR, "String ends outside addrspace\n");
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }

  retval = NaClOpenAclCheck(natp->nap, path, flags, mode);
  if (0 != retval) {
    NaClLog(3, "Open ACL check rejected \"%s\".\n", path);
    goto cleanup;
  }

  /*
   * Perform a stat to determine whether the file is a directory.
   *
   * NB: it is okay for the stat to fail, since the request may be to
   * create a new file.
   *
   * There is a race conditions here: between the stat and the
   * open-as-a-file and open-as-a-dir, the type of the object that the
   * path refers to can change.
   */
  retval = NaClHostDescStat(path, &stbuf);

  if (0 == retval && NACL_ABI_S_ISDIR(stbuf.nacl_abi_st_mode)) {
    struct NaClHostDir  *hd;

    hd = malloc(sizeof *hd);
    if (NULL == hd) {
      retval = -NACL_ABI_ENOMEM;
      goto cleanup;
    }
    retval = NaClHostDirOpen(hd, path);
    NaClLog(LOG_INFO, "NaClHostDirOpen(0x%08"PRIxPTR", %s) returned %d\n",
            (uintptr_t) hd, path, retval);
    if (0 == retval) {
      retval = NaClSetAvail(natp->nap,
                            ((struct NaClDesc *) NaClDescDirDescMake(hd)));
      NaClLog(LOG_INFO, "Entered directory into open file table at %d\n",
              retval);
    }
  } else {
    struct NaClHostDesc  *hd;

    hd = malloc(sizeof *hd);
    if (NULL == hd) {
      retval = -NACL_ABI_ENOMEM;
      goto cleanup;
    }
    retval = NaClHostDescOpen(hd, path, flags, mode);
    NaClLog(LOG_INFO,
            "NaClHostDescOpen(0x%08"PRIxPTR", %s, 0%o, 0%o) returned %d\n",
            (uintptr_t) hd, path, flags, mode, retval);
    if (0 == retval) {
      retval = NaClSetAvail(natp->nap,
                            ((struct NaClDesc *) NaClDescIoDescMake(hd)));
      NaClLog(LOG_INFO, "Entered into open file table at %d\n", retval);
    }
  }
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);

  return retval;
}

int32_t NaClCommonSysClose(struct NaClAppThread *natp,
                           int                  d) {
  int             retval = -NACL_ABI_EBADF;
  struct NaClDesc *ndp;

  NaClLog(4, "Entered NaClCommonSysClose(0x%08"PRIxPTR", %d)\n",
          (uintptr_t) natp, d);

  NaClSysCommonThreadSyscallEnter(natp);

  NaClXMutexLock(&natp->nap->desc_mu);
  ndp = NaClGetDescMu(natp->nap, d);
  if (NULL != ndp) {
    NaClSetDescMu(natp->nap, d, NULL);  /* Unref the desc_tbl */
  }
  NaClXMutexUnlock(&natp->nap->desc_mu);
  NaClLog(5, "Invoking Close virtual function of object 0x%08"PRIxPTR"\n",
          (uintptr_t) ndp);
  if (NULL != ndp) {
    retval = (*ndp->vtbl->Close)(ndp, natp->effp);  /* Unref */
  }

  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

int32_t NaClCommonSysGetdents(struct NaClAppThread *natp,
                              int                  d,
                              void                 *dirp,
                              size_t               count) {
  int             retval = -NACL_ABI_EINVAL;
  uintptr_t       sysaddr;
  struct NaClDesc *ndp;

  NaClLog(4,
          ("Entered NaClCommonSysGetdents(0x%08"PRIxPTR", %d, 0x%08"PRIxPTR","
           " %"PRIdS"[0x%"PRIxS"])\n"),
          (uintptr_t) natp, d, (uintptr_t) dirp, count, count);

  NaClSysCommonThreadSyscallEnter(natp);

  sysaddr = NaClUserToSysAddrRange(natp->nap, (uintptr_t) dirp, count);
  if (kNaClBadAddress == sysaddr) {
    NaClLog(4, " illegal address for directory data\n");
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }
  ndp = NaClGetDesc(natp->nap, d);
  if (NULL == ndp) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }
  retval = (*ndp->vtbl->Getdents)(ndp, natp->effp, (void *) sysaddr, count);
  if (retval > 0) {
    NaClLog(4, "getdents returned %d bytes\n", retval);
    NaClLog(8, "getdents result: %.*s\n", retval, (char *) sysaddr);
  } else {
    NaClLog(4, "getdents returned %d\n", retval);
  }
  NaClDescUnref(ndp);

cleanup:
  NaClSysCommonThreadSyscallLeave(natp);

  return retval;
}

int32_t NaClCommonSysRead(struct NaClAppThread  *natp,
                          int                   d,
                          void                  *buf,
                          size_t                count) {
  int32_t         retval = -NACL_ABI_EINVAL;
  uintptr_t       sysaddr;
  struct NaClDesc *ndp;

  NaClLog(4,
          ("Entered NaClCommonSysRead(0x%08"PRIxPTR", %d, 0x%08"PRIxPTR","
           " %"PRIdS"[0x%"PRIxS"])\n"),
          (uintptr_t) natp, d, (uintptr_t) buf, count, count);

  NaClSysCommonThreadSyscallEnter(natp);

  sysaddr = NaClUserToSysAddrRange(natp->nap, (uintptr_t) buf, count);
  if (kNaClBadAddress == sysaddr) {
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }
  ndp = NaClGetDesc(natp->nap, d);
  if (NULL == ndp) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }
  retval = (*ndp->vtbl->Read)(ndp, natp->effp, (void *) sysaddr, count);
  if (retval > 0) {
    NaClLog(4, "read returned %d bytes\n", retval);
    NaClLog(8, "read result: %.*s\n",
            retval, (char *) sysaddr);
  } else {
    NaClLog(4, "read returned %d\n", retval);
  }
  NaClDescUnref(ndp);
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);

  return retval;
}

int32_t NaClCommonSysWrite(struct NaClAppThread *natp,
                           int                  d,
                           void                 *buf,
                           size_t               count) {
  int32_t         retval = -NACL_ABI_EINVAL;
  uintptr_t       sysaddr;
  struct NaClDesc *ndp;

  NaClLog(4,
          "Entered NaClCommonSysWrite(0x%08"PRIxPTR", %d, 0x%08"PRIxPTR","
          " %"PRIdS"[0x%"PRIxS"])\n",
          (uintptr_t) natp, d, (uintptr_t) buf, count, count);

  NaClSysCommonThreadSyscallEnter(natp);

  sysaddr = NaClUserToSysAddrRange(natp->nap, (uintptr_t) buf, count);
  if (kNaClBadAddress == sysaddr) {
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }

  NaClLog(4, "In NaClSysWrite(%d, %.*s, %"PRIdS")\n",
          d, (int) count, (char *) sysaddr, count);

  ndp = NaClGetDesc(natp->nap, d);
  if (NULL == ndp) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }
  retval = (*ndp->vtbl->Write)(ndp, natp->effp, (void *) sysaddr, count);
  NaClDescUnref(ndp);

cleanup:
  NaClSysCommonThreadSyscallLeave(natp);

  return retval;
}

int32_t NaClCommonSysLseek(struct NaClAppThread *natp,
                           int                  d,
                           off_t                offset,
                           int                  whence) {
  int             retval = -NACL_ABI_EINVAL;
  struct NaClDesc *ndp;

  NaClLog(4,
          ("Entered NaClCommonSysLseek(0x%08"PRIxPTR", %d,"
           " 0x%08"PRIx64", %d)\n"),
          (uintptr_t) natp, d, (int64_t) offset, whence);

  NaClSysCommonThreadSyscallEnter(natp);

  ndp = NaClGetDesc(natp->nap, d);
  if (NULL == ndp) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }
  retval = (*ndp->vtbl->Seek)(ndp, natp->effp, offset, whence);
  NaClDescUnref(ndp);
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

int32_t NaClCommonSysIoctl(struct NaClAppThread *natp,
                           int                  d,
                           int                  request,
                           void                 *arg) {
  int             retval = -NACL_ABI_EINVAL;
  uintptr_t       sysaddr;
  struct NaClDesc *ndp;

  NaClLog(4, "In NaClSysIoctl(%d, %d, 0x%08"PRIxPTR")\n", d, request,
          (uintptr_t) arg);

  NaClSysCommonThreadSyscallEnter(natp);
  /*
   * Note that NaClUserToSysAddrRange is not feasible right now, since
   * the size of the arg argument depends on the request.  We do not
   * have an enumeration of allowed ioctl requests yet.
   *
   * Furthermore, some requests take no arguments, so sysaddr might
   * end up being kNaClBadAddress and that is perfectly okay.
   */
  sysaddr = NaClUserToSysAddr(natp->nap, (uintptr_t) arg);
  /*
   ****************************************
   * NOTE: sysaddr may be kNaClBadAddress *
   ****************************************
   */

  ndp = NaClGetDesc(natp->nap, d);
  if (NULL == ndp) {
    NaClLog(4, "bad desc\n");
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  retval = NaClIoctlAclCheck(natp->nap, ndp, request, arg);
  if (0 != retval) {
    NaClLog(3, "Ioctl ACL check rejected descriptor %d\n", d);
    goto cleanup_unref;
  }

  retval = (*ndp->vtbl->Ioctl)(ndp, natp->effp, request, (void *) sysaddr);
cleanup_unref:
  NaClDescUnref(ndp);
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

int32_t NaClCommonSysFstat(struct NaClAppThread *natp,
                           int                  d,
                           struct nacl_abi_stat *nasp) {
  int32_t         retval = -NACL_ABI_EINVAL;
  uintptr_t       sysaddr;
  struct NaClDesc *ndp;

  NaClLog(4, "In NaClSysFstat(%d, 0x%08"PRIxPTR")\n", d, (uintptr_t) nasp);

  NaClSysCommonThreadSyscallEnter(natp);

  sysaddr = NaClUserToSysAddrRange(natp->nap, (uintptr_t) nasp, sizeof *nasp);
  if (kNaClBadAddress == sysaddr) {
    NaClLog(4, "bad addr\n");
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }

  ndp = NaClGetDesc(natp->nap, d);
  if (NULL == ndp) {
    NaClLog(4, "bad desc\n");
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }
  retval = (*ndp->vtbl->Fstat)(ndp, natp->effp,
                               (struct nacl_abi_stat *) sysaddr);
  NaClDescUnref(ndp);
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);

  return retval;
}

int32_t NaClCommonSysStat(struct NaClAppThread  *natp,
                          const char            *pathname,
                          struct nacl_abi_stat  *buf) {
  int32_t              retval = -NACL_ABI_EINVAL;
  uintptr_t            syspathaddr;
  uintptr_t            sysbufaddr;
  char                 path[NACL_CONFIG_PATH_MAX];
  int                  len;

  NaClLog(4,
          ("In NaClCommonSysStat(0x%08"PRIxPTR", 0x%08"PRIxPTR","
           " 0x%08"PRIxPTR")\n"),
          (uintptr_t) natp, (uintptr_t) pathname, (uintptr_t) buf);

  NaClSysCommonThreadSyscallEnter(natp);

  syspathaddr = NaClUserToSysAddr(natp->nap, (uintptr_t) pathname);
  if (kNaClBadAddress == syspathaddr) {
    NaClLog(LOG_ERROR, "Invalid address for pathname\n");
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }
  /*
   * strncpy may (try to) get bytes that is outside the app's address
   * space and generate a fault.
   */
  strncpy(path, (char *) syspathaddr, sizeof path);
  /*
   * survived the copy, but did there happen to be data beyond the end?
   */
  path[sizeof path - 1] = '\0';  /* always null terminate */
  NaClLog(LOG_INFO, "NaClCommonSysStat: Path: %s\n", path);
  len = strlen(path);
  /*
   * make sure sysaddr is a string, and the whole string is in app
   * address space...
   *
   * address space is convex, so it is impossible for beginning and
   * end to be both in the address space and yet have an intermediate
   * byte not be in the address space.
   */
  if (kNaClBadAddress == NaClUserToSysAddr(natp->nap,
                                           len + (uintptr_t) pathname)) {
    NaClLog(LOG_ERROR, "String ends outside addrspace\n");
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }

  /*
   * Make sure result buffer is in the app's address space.
   */
  sysbufaddr = NaClUserToSysAddrRange(natp->nap, (uintptr_t) buf, sizeof *buf);
  if (kNaClBadAddress == sysbufaddr) {
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }

  retval = NaClStatAclCheck(natp->nap, path);
  if (0 != retval) {
    goto cleanup;
  }

  /*
   * Perform a host stat.
   */
  retval = NaClHostDescStat(path, (struct nacl_abi_stat *) sysbufaddr);
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

void NaClCommonUtilUpdateAddrMap(struct NaClAppThread *natp,
                                 uintptr_t            sysaddr,
                                 size_t               nbytes,
                                 int                  sysprot,
                                 struct NaClDesc      *backing_desc,
                                 size_t               backing_bytes,
                                 off_t                offset_bytes,
                                 int                  delete_mem) {
  uintptr_t                   usraddr;
  struct NaClMemObj           *nmop;

  NaClLog(3,
          ("NaClCommonUtilUpdateAddrMap(0x%08"PRIxPTR", 0x%08"PRIxPTR","
           " 0x%"PRIxS", 0x%x, 0x%08"PRIxPTR", 0x%"PRIxS", 0x%"PRIx64", %d)\n"),
          (uintptr_t) natp, sysaddr, nbytes,
          sysprot, (uintptr_t) backing_desc, backing_bytes,
          (int64_t) offset_bytes,
          delete_mem);
  usraddr = NaClSysToUser(natp->nap, sysaddr);
  nmop = NULL;
  /* delete_mem -> NULL == backing_desc */
  if (NULL != backing_desc) {
    if (delete_mem) {
      NaClLog(LOG_FATAL,
              ("invariant of delete_mem implies backing_desc NULL"
               " violated.\n"));
    }
    nmop = NaClMemObjMake(backing_desc, backing_bytes, offset_bytes);
  }

  NaClVmmapUpdate(&natp->nap->mem_map,
                  usraddr >> NACL_PAGESHIFT,
                  nbytes >> NACL_PAGESHIFT,
                  sysprot,
                  nmop,
                  delete_mem);
}


int NaClSysCommonAddrRangeContainsExecutablePages_mu(struct NaClApp *nap,
                                                     uintptr_t      usraddr,
                                                     size_t         length) {
  /*
   * NOTE: currently only trampoline and text region are executable,
   * and they are at the beginning of the address space, so this code
   * is fine.  We will probably never allow users to mark other pages
   * as executable; but if so, we will have to revisit how this check
   * is implemented.
   *
   * The sum NACL_TRAMPOLINE_END + nap->text_region_size is a
   * multiple of 4K, the memory protection granularity.  Since this
   * routine is used for checking whether memory map adjustments /
   * allocations -- which has 64K granularity -- is okay, usraddr must
   * be an allocation granularity value.  Our callers (as of this
   * writing) does this, but we truncate it down to an allocation
   * boundary to be sure.
   */
  UNREFERENCED_PARAMETER(length);
  usraddr = NaClTruncAllocPage(usraddr);
  return usraddr < NACL_TRAMPOLINE_END + nap->text_region_bytes;
}


/* Warning: sizeof(nacl_abi_off_t)!=sizeof(off_t) on OSX */
int32_t NaClCommonSysMmap(struct NaClAppThread  *natp,
                          void                  *start,
                          size_t                length,
                          int                   prot,
                          int                   flags,
                          int                   d,
                          nacl_abi_off_t        offset) {
  int32_t                     retval = -NACL_ABI_EINVAL;
  int                         allowed_flags;
  struct NaClDesc             *ndp;
  uintptr_t                   usraddr;
  uintptr_t                   usrpage;
  uintptr_t                   sysaddr;
  uintptr_t                   endaddr;
  void                        *map_result;
  int                         holding_app_lock;
  struct NaClMemObj           *nmop;
  struct nacl_abi_stat        stbuf;
  size_t                      alloc_rounded_length;
  off_t                       file_size;
  size_t                      file_bytes;
  size_t                      alloc_rounded_file_bytes;
  size_t                      start_of_inaccessible;

  holding_app_lock = 0;
  nmop = NULL;

  allowed_flags = (NACL_ABI_MAP_FIXED | NACL_ABI_MAP_SHARED
                   | NACL_ABI_MAP_PRIVATE | NACL_ABI_MAP_ANONYMOUS);

  usraddr = (uintptr_t) start;

  NaClLog(4,
          "NaClSysMmap(0x%08"PRIxPTR",0x%"PRIxS",0x%x,0x%x,%d,0x%08"PRIx32")\n",
          usraddr, length, prot, flags, d, (int32_t) offset);

  NaClSysCommonThreadSyscallEnter(natp);

  if (0 != (flags & ~allowed_flags)) {
    NaClLog(LOG_WARNING, "invalid mmap flags 0%o, ignoring extraneous bits",
            flags);
    flags &= allowed_flags;
  }

  if (0 != (flags & NACL_ABI_MAP_ANONYMOUS)) {
    /*
     * anonymous mmap, so backing store is just swap: no descriptor is
     * involved, and no memory object will be created to represent the
     * descriptor.
     */
    ndp = NULL;
  } else {
    ndp = NaClGetDesc(natp->nap, d);
    if (NULL == ndp) {
      retval = -NACL_ABI_EBADF;
      goto cleanup;
    }
  }

  /*
   * Starting address must be aligned to worst-case allocation
   * granularity.  (Windows.)  We round up.
   */
  if (!NaClIsAllocPageMultiple(usraddr)) {
    NaClLog(2, "NaClSysMmap: address not allocation granularity aligned\n");
    usraddr = NaClRoundAllocPage(usraddr);
  }
  /*
   * And offset must be a multiple of the allocation unit.
   */
  if (!NaClIsAllocPageMultiple((uintptr_t) offset)) {
    NaClLog(LOG_INFO,
            ("NaClSysMmap: file offset 0x%08"PRIxPTR" not multiple"
             " of allocation size\n"),
            (uintptr_t) offset);
    retval = -NACL_ABI_EINVAL;
    goto cleanup;
  }

  if (0 == length) {
    retval = -NACL_ABI_EINVAL;
    goto cleanup;
  }
  alloc_rounded_length = NaClRoundAllocPage(length);
  if (alloc_rounded_length != length) {
    NaClLog(LOG_WARNING,
            "mmap: rounded length to 0x%"PRIxS"\n", alloc_rounded_length);
  }

  if (NULL == ndp) {
    /*
     * sentinel values that are bigger than the addr space
     */
    file_size = ~0u>>1;
    file_bytes = file_size;
    alloc_rounded_file_bytes = file_size;
  } else {
    /*
     * We stat the file to figure out its actual size.
     *
     * This is needed since we allow an app to mmap in a odd-sized
     * file and will zero fill the allocation page containing the last
     * byte(s) of the file, but if the app asked for a length that
     * goes beyond the last allocation page, that memory is actually
     * inaccessible.  Of course, the underlying OS deals with real
     * pages, and we may need to simulate this behavior (i.e., OSX and
     * Linux, we will need to put zero-filled pages between the last
     * 4K system page containing file data and the rest of the
     * simulated windows allocation 64K page.
     */
    retval = (*ndp->vtbl->Fstat)(ndp, natp->effp, &stbuf);
    if (0 != retval) {
      goto cleanup;
    }
    /*
     * BUG(bsy): there's a race between this fstat and the actual mmap
     * below.  It's probably insoluble.  Even if we fstat again after
     * mmap and compared, the mmap could have "seen" the file with a
     * different size, after which the racing thread restored back to
     * the same value before the 2nd fstat takes place.
     */
    file_size = stbuf.nacl_abi_st_size;

    if (file_size < offset) {
      retval = -NACL_ABI_EINVAL;
      goto cleanup;
    }
    file_bytes = NaClRoundHostAllocPage(file_size - offset);
    /*
     * We need to deal with NaClRoundHostAllocPage rounding up to zero
     * from ~0u - n, where n < 4096 or 65536 (== 1 alloc page).
     */
    alloc_rounded_file_bytes = NaClRoundAllocPage(file_bytes);

    if (0 == alloc_rounded_file_bytes && 0 != file_bytes) {
      retval = -NACL_ABI_ENOMEM;
      goto cleanup;
    }

    /*
     * NB: file_bytes and alloc_rounded_file_bytes can be zero.  Such
     * an mmap just makes memory (offset relative to usraddr) in the
     * range [0, alloc_rounded_length) inaccessible.
     */
  }

  /*
   * file_bytes is how many bytes we can map from the file, given the
   * user-supplied starting offset.  It is at least one page.  If it
   * came from a real file, it is a multiple of host-OS allocation
   * size.
   */
  length = size_min(alloc_rounded_length, file_bytes);
  start_of_inaccessible = size_min(alloc_rounded_length,
                                   alloc_rounded_file_bytes);

  /*
   * Now, we map, relative to usraddr, bytes [0, length) from the file
   * starting at offset, zero-filled pages for the memory region
   * [length, start_of_inaccessible), and inaccessible pages for the
   * memory region [start_of_inaccessible, alloc_rounded_length).
   */

  /*
   * Lock the addr space.
   */
  NaClXMutexLock(&natp->nap->mu);
  holding_app_lock = 1;

  if (0 == (flags & NACL_ABI_MAP_FIXED)) {
    /*
     * The user wants us to pick an address range.
     */
    if (0 == usraddr) {
      /*
       * Pick a hole in addr space of appropriate size, anywhere.
       * We pick one that's best for the system.
       */
      usrpage = NaClVmmapFindMapSpace(&natp->nap->mem_map,
                                      alloc_rounded_length >> NACL_PAGESHIFT);
      NaClLog(4, "NaClSysMmap: FindMapSpace: page 0x%05"PRIxPTR"\n", usrpage);
      if (0 == usrpage) {
        retval = -NACL_ABI_ENOMEM;
        goto cleanup;
      }
      usraddr = usrpage << NACL_PAGESHIFT;
      NaClLog(4, "NaClSysMmap: new starting addr: 0x%08"PRIxPTR"\n", usraddr);
    } else {
      /*
       * user supplied an addr, but it's to be treated as a hint; we
       * find a hole of the right size in the app's address space,
       * according to the usual mmap semantics.
       */
      usrpage = NaClVmmapFindMapSpaceAboveHint(&natp->nap->mem_map,
                                               usraddr,
                                               (alloc_rounded_length
                                                >> NACL_PAGESHIFT));
      NaClLog(4, "NaClSysMmap: FindSpaceAboveHint: page 0x%05"PRIxPTR"\n",
              usrpage);
      if (0 == usrpage) {
        NaClLog(4, "NaClSysMmap: hint failed, doing generic allocation\n");
        usrpage = NaClVmmapFindMapSpace(&natp->nap->mem_map,
                                        alloc_rounded_length >> NACL_PAGESHIFT);
      }
      if (0 == usrpage) {
        retval = -NACL_ABI_ENOMEM;
        goto cleanup;
      }
      usraddr = usrpage << NACL_PAGESHIFT;
      NaClLog(4, "NaClSysMmap: new starting addr: 0x%08"PRIxPTR"\n", usraddr);
    }
  }

  /*
   * Validate [usraddr, endaddr) is okay.
   */
  if (usraddr >= (1U << natp->nap->addr_bits)) {
    NaClLog(2,
            ("NaClSysMmap: start address (0x%08"PRIxPTR") outside address"
             " space\n"),
            usraddr);
    retval = -NACL_ABI_EINVAL;
    goto cleanup;
  }
  endaddr = usraddr + alloc_rounded_length;
  if (endaddr < usraddr) {
    NaClLog(0,
            ("NaClSysMmap: integer overflow -- "
             "NaClSysMmap(0x%08"PRIxPTR",0x%"PRIxS",0x%x,0x%x,%d,"
             "0x%08"PRIxPTR"\n"),
            usraddr, length, prot, flags, d, (uintptr_t) offset);
    retval = -NACL_ABI_EINVAL;
    goto cleanup;
  }
  /*
   * NB: we use > instead of >= here.
   *
   * endaddr is the address of the first byte beyond the target region
   * and it can equal the address space limit.  (of course, normally
   * the main thread's stack is there.)
   */
  if (endaddr > (1U << natp->nap->addr_bits)) {
    NaClLog(2,
            ("NaClSysMmap: end address (0x%08"PRIxPTR") is beyond"
             " the end of the address space\n"),
            endaddr);
    retval = -NACL_ABI_EINVAL;
    goto cleanup;
  }

  if (NaClSysCommonAddrRangeContainsExecutablePages_mu(natp->nap,
                                                       usraddr,
                                                       length)) {
    NaClLog(2, "NaClSysMmap: region contains executable pages\n");
    retval = -NACL_ABI_EINVAL;
    goto cleanup;
  }

  /*
   * Force NACL_ABI_MAP_FIXED, since we are specifying address in NaCl
   * app address space.
   */
  flags |= NACL_ABI_MAP_FIXED;

  /*
   * Never allow users to say that mmapped pages are executable.  This
   * is primarily for the service runtime's own bookkeeping -- prot is
   * used in NaClCommonUtilUpdateAddrMap -- since %cs restriction
   * makes page protection irrelevant, it doesn't matter that on many
   * systems (w/o NX) PROT_READ implies PROT_EXEC.
   */
  prot &= ~NACL_ABI_PROT_EXEC;

  /*
   * Exactly one of NACL_ABI_MAP_SHARED and NACL_ABI_MAP_PRIVATE is set.
   */
  if ((0 == (flags & NACL_ABI_MAP_SHARED))
      == (0 == (flags & NACL_ABI_MAP_PRIVATE))) {
    retval = -NACL_ABI_EINVAL;
    goto cleanup;
  }

  sysaddr = NaClUserToSys(natp->nap, usraddr);

  /* [0, length) */
  if (length > 0) {
    if (NULL == ndp) {
      NaClLog(4,
              ("NaClSysMmap: NaClDescIoDescMap(,,0x%08"PRIxPTR","
               "0x%08"PRIxS",0x%x,0x%x,0x%08"PRIxPTR")\n"),
              sysaddr, length, prot, flags, (uintptr_t) offset);
      map_result = (void *) NaClDescIoDescMap(NULL,
                                              natp->effp,
                                              (void *) sysaddr,
                                              length,
                                              prot,
                                              flags,
                                              (off_t) offset);
    } else {
      NaClLog(4,
              ("NaClSysMmap: (*ndp->vtbl->Map)(,,0x%08"PRIxPTR","
               "0x%08"PRIxS",0x%x,0x%x,0x%08"PRIxPTR")\n"),
              sysaddr, length, prot, flags, (uintptr_t) offset);

      map_result = (void *) (*ndp->vtbl->Map)(ndp,
                                              natp->effp,
                                              (void *) sysaddr,
                                              length,
                                              prot,
                                              flags,
                                              (off_t) offset);
    }
    /*
     * "Small" negative integers are errno values.  Larger ones are
     * virtual addresses.
     */
    if (NaClIsNegErrno((uintptr_t) map_result)) {
      NaClLog(LOG_FATAL,
              ("NaClSysMmap: Map failed, but we"
               " cannot handle address space move, error %"PRIuS""),
              (size_t) map_result);
    }
    if (map_result != (void *) sysaddr) {
      NaClLog(LOG_FATAL, "system mmap did not honor NACL_ABI_MAP_FIXED\n");
    }
    if (prot == NACL_ABI_PROT_NONE) {
      /*
       * windows nacl_host_desc implementation requires that PROT_NONE
       * memory be freed using VirtualFree rather than
       * UnmapViewOfFile.  TODO(bsy): remove this ugliness.
       */
      NaClCommonUtilUpdateAddrMap(natp, sysaddr, length, PROT_NONE,
                                  NULL, file_size, offset, 0);
    } else {
      /* record change for file-backed memory */
      NaClCommonUtilUpdateAddrMap(natp, sysaddr, length, NaClProtMap(prot),
                                  ndp, file_size, offset, 0);
    }
  } else {
    map_result = (void *) sysaddr;
  }
  /* zero fill [length, start_of_inaccessible) */
  if (length < start_of_inaccessible) {
    size_t  map_len = start_of_inaccessible - length;

    NaClLog(2,
            ("zero-filling pages for memory range"
             " [0x%08"PRIxPTR", 0x%08"PRIxPTR"), length 0x%"PRIxS"\n"),
            sysaddr + length, sysaddr + start_of_inaccessible, map_len);
    retval = NaClHostDescMap((struct NaClHostDesc *) NULL,
                             (void *) (sysaddr + length),
                             map_len,
                             prot,
                             NACL_ABI_MAP_ANONYMOUS | NACL_ABI_MAP_PRIVATE,
                             (off_t) 0);
    if (NaClIsNegErrno(retval)) {
      NaClLog(LOG_ERROR,
              ("Could not create zero-filled pages for memory range"
               " [0x%08"PRIxPTR", 0x%08"PRIxPTR")\n"),
              sysaddr + length, sysaddr + start_of_inaccessible);
      goto cleanup;
    }
    NaClCommonUtilUpdateAddrMap(natp, sysaddr + length, map_len,
                                NaClProtMap(prot),
                                (struct NaClDesc *) NULL, 0, (off_t) 0, 0);
  }
  /* inaccessible: [start_of_inaccessible, alloc_rounded_length) */
  if (start_of_inaccessible < alloc_rounded_length) {
    size_t  map_len = alloc_rounded_length - start_of_inaccessible;

    NaClLog(2,
            ("inaccessible pages for memory range"
             " [0x%08"PRIxPTR", 0x%08"PRIxPTR"), length 0x%"PRIxS"\n"),
            sysaddr + start_of_inaccessible,
            sysaddr + alloc_rounded_length,
            map_len);
    retval = NaClHostDescMap((struct NaClHostDesc *) NULL,
                             (void *) (sysaddr + start_of_inaccessible),
                             map_len,
                             NACL_ABI_PROT_NONE,
                             NACL_ABI_MAP_ANONYMOUS | NACL_ABI_MAP_PRIVATE,
                             (off_t) 0);
    if (NaClIsNegErrno(retval)) {
      NaClLog(LOG_ERROR,
            ("Could not create inaccessible pages for memory range"
             " [0x%08"PRIxPTR", 0x%08"PRIxPTR"), length 0x%"PRIxS"\n"),
            sysaddr + start_of_inaccessible,
            sysaddr + alloc_rounded_length,
            map_len);
    }
    NaClCommonUtilUpdateAddrMap(natp, sysaddr + start_of_inaccessible,
                                map_len, PROT_NONE,
                                (struct NaClDesc *) NULL, 0,
                                (off_t) 0, 0);
  }
  NaClLog(3, "NaClSysMmap: got address 0x%08"PRIxPTR"\n",
          (uintptr_t) map_result);

  retval = usraddr;
cleanup:
  if (holding_app_lock) {
    NaClXMutexUnlock(&natp->nap->mu);
  }
  if (NULL != ndp) {
    NaClDescUnref(ndp);
  }
  if (NaClIsNegErrno((uintptr_t) retval)) {
    free(nmop);
  }

  NaClSysCommonThreadSyscallLeave(natp);

  NaClLog(3, "NaClSysMmap: returning 0x%08x\n", retval);
  return retval;
}

int32_t NaClCommonSysImc_MakeBoundSock(struct NaClAppThread *natp,
                                       int                  *sap) {
  /*
   * Create a bound socket descriptor and a socket address descriptor.
   */

  int32_t                     retval = -NACL_ABI_EINVAL;
  uintptr_t                   sys_sap;
  struct NaClDesc             *pair[2];

  NaClLog(3,
          ("Entered NaClCommonSysImc_MakeBoundSock(0x%08"PRIxPTR","
           " 0x%08"PRIxPTR")\n"),
          (uintptr_t) natp, (uintptr_t) sap);

  NaClSysCommonThreadSyscallEnter(natp);

  sys_sap = NaClUserToSysAddrRange(natp->nap,
                                   (uintptr_t) sap,
                                   2 * sizeof *sap);
  if (kNaClBadAddress == sys_sap) {
    NaClLog(3, " illegal address\n");
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }

  retval = NaClCommonDescMakeBoundSock(pair);
  if (0 != retval) {
    goto cleanup;
  }

  ((int *) sys_sap)[0] = NaClSetAvail(natp->nap, pair[0]);
  ((int *) sys_sap)[1] = NaClSetAvail(natp->nap, pair[1]);
  retval = 0;
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);

  return retval;
}

int32_t NaClCommonSysImc_Accept(struct NaClAppThread  *natp,
                                int                   d) {
  int32_t         retval = -NACL_ABI_EINVAL;
  struct NaClDesc *ndp;

  NaClLog(4, "Entered NaClSysImc_Accept(0x%08"PRIxPTR", %d)\n",
          (uintptr_t) natp, d);

  NaClSysCommonThreadSyscallEnter(natp);

  ndp = NaClGetDesc(natp->nap, d);
  if (NULL == ndp) {
    retval = -NACL_ABI_EBADF;
  } else {
    retval = (*ndp->vtbl->AcceptConn)(ndp, natp->effp);
    NaClDescUnref(ndp);
  }

  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

int32_t NaClCommonSysImc_Connect(struct NaClAppThread *natp,
                                 int                  d) {
  int32_t         retval = -NACL_ABI_EINVAL;
  struct NaClDesc *ndp;

  NaClLog(4, "Entered NaClSysImc_ConnectAddr(0x%08"PRIxPTR", %d)\n",
          (uintptr_t) natp, d);

  NaClSysCommonThreadSyscallEnter(natp);

  ndp = NaClGetDesc(natp->nap, d);
  if (NULL == ndp) {
    retval = -NACL_ABI_EBADF;
  } else {
    retval = (*ndp->vtbl->ConnectAddr)(ndp, natp->effp);
    NaClDescUnref(ndp);
  }

  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

/*
 * This function converts addresses from user addresses to system
 * addresses, copying into kernel space as needed to avoid TOCvTOU
 * races, then invoke NaClImcSendTypedMessage from the nrd_xfer
 * library.
 */
int32_t NaClCommonSysImc_Sendmsg(struct NaClAppThread *natp,
                                 int                  d,
                                 struct NaClImcMsgHdr *nimhp,
                                 int                  flags) {
  int32_t                   retval = -NACL_ABI_EINVAL;
  uintptr_t                 sysaddr;
  /* copy of user-space data for validation */
  struct NaClImcMsgHdr      kern_nimh;
  struct NaClImcMsgIoVec    kern_iov[NACL_ABI_IMC_IOVEC_MAX];
  /* kernel-side representatin of descriptors */
  struct NaClDesc           *kern_desc[NACL_ABI_IMC_USER_DESC_MAX];
  struct NaClImcTypedMsgHdr kern_msg_hdr;
  struct NaClDesc           *ndp;
  size_t                    i;

  NaClLog(3,
          ("Entered NaClCommonSysImc_Sendmsg(0x%08"PRIxPTR", %d,"
           " 0x%08"PRIxPTR", 0x%x)\n"),
          (uintptr_t) natp, d, (uintptr_t) nimhp, flags);

  NaClSysCommonThreadSyscallEnter(natp);

  sysaddr = NaClUserToSysAddrRange(natp->nap,
                                   (uintptr_t) nimhp,
                                   sizeof *nimhp);
  if (kNaClBadAddress == sysaddr) {
    NaClLog(4, "NaClImcMsgHdr not in user address space\n");
    retval = -NACL_ABI_EFAULT;
    goto cleanup_leave;
  }

  kern_nimh = *(struct NaClImcMsgHdr *) sysaddr;  /* copy before validating */

  /*
   * Some of these checks duplicate checks that will be done in the
   * nrd xfer library, but it is better to check before doing the
   * address translation of memory/descriptor vectors if those vectors
   * might be too long.  Plus, we need to copy and validate vectors
   * for TOCvTOU race protection, and we must prevent overflows.  The
   * nrd xfer library's checks should never fire when called from the
   * service runtime, but the nrd xfer library might be called from
   * other code.
   */
  if (kern_nimh.iov_length > NACL_ABI_IMC_IOVEC_MAX) {
    NaClLog(4, "gather/scatter array too large\n");
    retval = -NACL_ABI_EINVAL;
    goto cleanup_leave;
  }
  if (kern_nimh.desc_length > NACL_ABI_IMC_USER_DESC_MAX) {
    NaClLog(4, "handle vector too long\n");
    retval = -NACL_ABI_EINVAL;
    goto cleanup_leave;
  }

  if (kern_nimh.iov_length > 0) {
    sysaddr = NaClUserToSysAddrRange(natp->nap,
                                     (uintptr_t) kern_nimh.iov,
                                     (kern_nimh.iov_length
                                      * sizeof *kern_nimh.iov));
    if (kNaClBadAddress == sysaddr) {
      NaClLog(4, "gather/scatter array not in user address space\n");
      retval = -NACL_ABI_EFAULT;
      goto cleanup_leave;
    }

    memcpy(kern_iov, (void *) sysaddr,
           kern_nimh.iov_length * sizeof *kern_nimh.iov);

    for (i = 0; i < kern_nimh.iov_length; ++i) {
      sysaddr = NaClUserToSysAddrRange(natp->nap,
                                       (uintptr_t) kern_iov[i].base,
                                       kern_iov[i].length);
      if (kNaClBadAddress == sysaddr) {
        retval = -NACL_ABI_EFAULT;
        goto cleanup_leave;
      }
      kern_iov[i].base = (void *) sysaddr;
    }
  }

  ndp = NaClGetDesc(natp->nap, d);
  if (NULL == ndp) {
    retval = -NACL_ABI_EBADF;
    goto cleanup_leave;
  }

  /*
   * make things easier for cleaup exit processing
   */
  memset(kern_desc, 0, sizeof kern_desc);
  retval = -NACL_ABI_EINVAL;

  kern_msg_hdr.iov = kern_iov;
  kern_msg_hdr.iov_length = kern_nimh.iov_length;

  if (0 == kern_nimh.desc_length) {
    kern_msg_hdr.ndescv = 0;
    kern_msg_hdr.ndesc_length = 0;
  } else {
    sysaddr = NaClUserToSysAddrRange(natp->nap,
                                     (uintptr_t) kern_nimh.descv,
                                     kern_nimh.desc_length * sizeof(int));
    if (kNaClBadAddress == sysaddr) {
      retval = -NACL_ABI_EFAULT;
      goto cleanup;
    }

    /*
     * NB: for each descv entry, we read from NaCl app address space
     * exactly once.
     */
    for (i = 0; i < kern_nimh.desc_length; ++i) {
      kern_desc[i] = NaClGetDesc(natp->nap, ((int *) sysaddr)[i]);
      if (NULL == kern_desc[i]) {
        retval = -NACL_ABI_EBADF;
        goto cleanup;
      }
    }
    kern_msg_hdr.ndescv = kern_desc;
    kern_msg_hdr.ndesc_length = kern_nimh.desc_length;
  }
  kern_msg_hdr.flags = kern_nimh.flags;

  retval = NaClImcSendTypedMessage(ndp, natp->effp, &kern_msg_hdr, flags);

  if (retval < 0) {
    /*
     * NaClWouldBlock uses TSD (for both the errno-based and
     * GetLastError()-based implementations), so this is threadsafe.
     */
    if (0 != (flags & NACL_DONT_WAIT) && NaClWouldBlock()) {
      retval = -NACL_ABI_EAGAIN;
    } else {
      /*
       * TODO(bsy): the else case is some mysterious internal error.
       * Should we destroy the ndp or otherwise mark it as bad?  Was
       * the failure atomic?  Did it send some partial data?  Linux
       * implementation appears okay.
       */
      retval = -NACL_ABI_EIO;
    }
  }

cleanup:
  for (i = 0; i < kern_nimh.desc_length; ++i) {
    if (NULL != kern_desc[i]) {
      NaClDescUnref(kern_desc[i]);
      kern_desc[i] = NULL;
    }
  }
  NaClDescUnref(ndp);
cleanup_leave:
  NaClSysCommonThreadSyscallLeave(natp);
  NaClLog(3, "NaClCommonSysImc_Sendmsg: returning %d\n", retval);
  return retval;
}

int32_t NaClCommonSysImc_Recvmsg(struct NaClAppThread *natp,
                                 int                  d,
                                 struct NaClImcMsgHdr *nimhp,
                                 int                  flags) {
  int                       retval = -NACL_ABI_EINVAL;
  uintptr_t                 sysaddr;
  struct NaClImcMsgHdr      *kern_nimhp;
  size_t                    i;
  struct NaClDesc           *ndp;
  struct NaClImcMsgHdr      kern_nimh;
  struct NaClImcMsgIoVec    kern_iov[NACL_ABI_IMC_IOVEC_MAX];
  int                       *kern_descv;
  struct NaClImcTypedMsgHdr recv_hdr;
  struct NaClDesc           *new_desc[NACL_ABI_IMC_DESC_MAX];
  size_t                    num_user_desc;

  NaClLog(3,
          ("Entered NaClCommonSysImc_RecvMsg(0x%08"PRIxPTR", %d,"
           " 0x%08"PRIxPTR")\n"),
          (uintptr_t) natp, d, (uintptr_t) nimhp);

  NaClSysCommonThreadSyscallEnter(natp);

  /*
   * First, we validate user-supplied message headers before
   * allocating a receive buffer.
   */
  sysaddr = NaClUserToSysAddrRange(natp->nap,
                                   (uintptr_t) nimhp,
                                   sizeof *nimhp);
  if (kNaClBadAddress == sysaddr) {
    NaClLog(4, "NaClImcMsgHdr not in user address space\n");
    retval = -NACL_ABI_EFAULT;
    goto cleanup_leave;
  }
  kern_nimhp = (struct NaClImcMsgHdr *) sysaddr;
  kern_nimh = *kern_nimhp;  /* copy before validating */
  if (kern_nimh.iov_length > NACL_ABI_IMC_IOVEC_MAX) {
    NaClLog(4, "gather/scatter array too large\n");
    retval = -NACL_ABI_EINVAL;
    goto cleanup_leave;
  }
  if (kern_nimh.desc_length > NACL_ABI_IMC_USER_DESC_MAX) {
    NaClLog(4, "handle vector too long\n");
    retval = -NACL_ABI_EINVAL;
    goto cleanup_leave;
  }

  if (kern_nimh.iov_length > 0) {
    sysaddr = NaClUserToSysAddrRange(natp->nap,
                                     (uintptr_t) kern_nimh.iov,
                                     (kern_nimh.iov_length
                                      * sizeof *kern_nimh.iov));
    if (kNaClBadAddress == sysaddr) {
      NaClLog(4, "gather/scatter array not in user address space\n");
      retval = -NACL_ABI_EFAULT;
      goto cleanup_leave;
    }
    /*
     * Copy IOV array into kernel space.  Validate this snapshot and do
     * user->kernel address conversions on this snapshot.
     */
    memcpy(kern_iov, (void *) sysaddr,
           kern_nimh.iov_length * sizeof *kern_nimh.iov);
    /*
     * Convert every IOV base from user to system address, validate
     * range of bytes are really in user address space.
     */

    for (i = 0; i < kern_nimh.iov_length; ++i) {
      sysaddr = NaClUserToSysAddrRange(natp->nap,
                                       (uintptr_t) kern_iov[i].base,
                                       kern_iov[i].length);
      if (kNaClBadAddress == sysaddr) {
        NaClLog(4, "iov number %"PRIdS" not entirely in user space\n", i);
        retval = -NACL_ABI_EFAULT;
        goto cleanup_leave;
      }
      kern_iov[i].base = (void *) sysaddr;
    }
  }

  if (kern_nimh.desc_length > 0) {
    sysaddr = NaClUserToSysAddrRange(natp->nap,
                                     (uintptr_t) kern_nimh.descv,
                                     kern_nimh.desc_length * sizeof(int));
    if (kNaClBadAddress == sysaddr) {
      retval = -NACL_ABI_EFAULT;
      goto cleanup_leave;
    }
    kern_descv = (int *) sysaddr;
  } else {
    /* ensure we will SEGV if there's a bug below */
    kern_descv = (int *) NULL;
  }

  ndp = NaClGetDesc(natp->nap, d);
  if (NULL == ndp) {
    NaClLog(4, "receiving descriptor invalid\n");
    retval = -NACL_ABI_EBADF;
    goto cleanup_leave;
  }

  recv_hdr.iov = kern_iov;
  recv_hdr.iov_length = kern_nimh.iov_length;

  recv_hdr.ndescv = new_desc;
  recv_hdr.ndesc_length = NACL_ARRAY_SIZE(new_desc);
  memset(new_desc, 0, sizeof new_desc);

  recv_hdr.flags = 0;  /* just to make it obvious; IMC will clear it for us */

  retval = NaClImcRecvTypedMessage(ndp, natp->effp, &recv_hdr, flags);
  /*
   * retval is number of user payload bytes received and excludes the
   * header bytes.
   */
  NaClLog(3, "NaClCommonSysImc_RecvMsg: NaClImcRecvTypedMessage returned %d\n",
          retval);
  if (retval < 0) {
    goto cleanup;
  }

  /*
   * NB: recv_hdr.flags may contain NACL_ABI_MESSAGE_TRUNCATED and/or
   * NACL_ABI_HANDLES_TRUNCATED.
   */

  kern_nimh.flags = recv_hdr.flags;

  /*
   * Now internalize the NaClHandles as NaClDesc objects.
   */
  num_user_desc = recv_hdr.ndesc_length;

  if (kern_nimh.desc_length < num_user_desc) {
    kern_nimh.flags |= NACL_ABI_RECVMSG_DESC_TRUNCATED;
    for (i = kern_nimh.desc_length; i < num_user_desc; ++i) {
      NaClDescUnref(new_desc[i]);
      new_desc[i] = NULL;
    }
    num_user_desc = kern_nimh.desc_length;
  }

  for (i = 0; i < num_user_desc; ++i) {
    /* write out to user space the descriptor numbers */
    kern_descv[i] = NaClSetAvail(natp->nap, new_desc[i]);
    new_desc[i] = NULL;
  }

  kern_nimh.desc_length = num_user_desc;
  *kern_nimhp = kern_nimh;
  /* copy out updated desc count, flags */
 cleanup:
  if (retval < 0) {
    for (i = 0; i < NACL_ARRAY_SIZE(new_desc); ++i) {
      if (NULL != new_desc[i]) {
        NaClDescUnref(new_desc[i]);
        new_desc[i] = NULL;
      }
    }
  }
  NaClDescUnref(ndp);
  NaClLog(3, "NaClCommonSysImc_RecvMsg: returning %d\n", retval);
cleanup_leave:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

int32_t NaClCommonSysImc_Mem_Obj_Create(struct NaClAppThread  *natp,
                                        size_t                size) {
  int32_t               retval = -NACL_ABI_EINVAL;
  struct NaClDescImcShm *shmp;
  NaClHandle            mem_obj;
  off_t                 size_as_off;

  if (0 != (size & (NACL_MAP_PAGESIZE - 1))) {
    return -NACL_ABI_EINVAL;
  }
  /*
   * TODO(bsy): policy about maximum shm object size should be
   * enforced here.
   */
  size_as_off = (off_t) size;
  if (size_as_off < 0) {
    return -NACL_ABI_EINVAL;
  }

  shmp = NULL;
  mem_obj = NACL_INVALID_HANDLE;

  NaClSysCommonThreadSyscallEnter(natp);

  shmp = malloc(sizeof *shmp);
  if (NULL == shmp) {
    retval = -NACL_ABI_ENOMEM;
    goto cleanup;
  }
  mem_obj = NaClCreateMemoryObject(size);
  if (NACL_INVALID_HANDLE == mem_obj) {
    retval = -NACL_ABI_ENOMEM;
    goto cleanup;
  }

  if (!NaClDescImcShmCtor(shmp, mem_obj, size_as_off)) {
    retval = -NACL_ABI_ENOMEM;  /* is this reasonable? */
    goto cleanup;
  }
  mem_obj = NACL_INVALID_HANDLE;

  retval = NaClSetAvail(natp->nap, (struct NaClDesc *) shmp);
  shmp = NULL;
cleanup:
  if (shmp) {
    free(shmp);
  }
  if (NACL_INVALID_HANDLE != mem_obj) {
    (void) NaClClose(mem_obj);
  }
  NaClSysCommonThreadSyscallLeave(natp);

  return retval;
}

int32_t NaClCommonDescSocketPair(struct NaClDesc      **pair) {
  int32_t                         retval = -NACL_ABI_EIO;
  struct NaClDescXferableDataDesc *d0;
  struct NaClDescXferableDataDesc *d1;
  NaClHandle                      sock_pair[2];

  /*
   * mark resources to enable easy cleanup
   */
  d0 = NULL;
  d1 = NULL;
  sock_pair[0] = NACL_INVALID_HANDLE;
  sock_pair[1] = NACL_INVALID_HANDLE;

  if (0 != NaClSocketPair(sock_pair)) {
    NaClLog(1,
            "NaClCommonSysImc_Socket_Pair: IMC socket pair creation failed\n");
    retval = -NACL_ABI_ENFILE;
    goto cleanup;
  }
  if (NULL == (d0 = malloc(sizeof *d0))) {
    retval = -NACL_ABI_ENOMEM;
    goto cleanup;
  }
  if (NULL == (d1 = malloc(sizeof *d1))) {
    free((void *) d0);
    d0 = NULL;
    retval = -NACL_ABI_ENOMEM;
    goto cleanup;
  }
  if (!NaClDescXferableDataDescCtor(d0, sock_pair[0])) {
    free((void *) d0);
    d0 = NULL;
    free((void *) d1);
    d1 = NULL;
    retval = -NACL_ABI_ENFILE;
    goto cleanup;
  }
  sock_pair[0] = NACL_INVALID_HANDLE;  /* ctor took ownership */
  if (!NaClDescXferableDataDescCtor(d1, sock_pair[1])) {
    free((void *) d1);
    d1 = NULL;
    retval = -NACL_ABI_ENFILE;
    goto cleanup;
  }
  sock_pair[1] = NACL_INVALID_HANDLE;  /* ctor took ownership */

  pair[0] = (struct NaClDesc *) d0;
  d0 = NULL;

  pair[1] = (struct NaClDesc *) d1;
  d1 = NULL;

  retval = 0;

 cleanup:
  /*
   * pre: d0 and d1 must either be NULL or point to fully constructed
   * NaClDesc objects
   */
  if (NULL != d0) {
    NaClDescUnref((struct NaClDesc *) d0);
  }
  if (NULL != d1) {
    NaClDescUnref((struct NaClDesc *) d1);
  }
  if (NACL_INVALID_HANDLE != sock_pair[0]) {
    NaClClose(sock_pair[0]);
  }
  if (NACL_INVALID_HANDLE != sock_pair[1]) {
    NaClClose(sock_pair[1]);
  }

  free(d0);
  free(d1);

  return retval;
}

int32_t NaClCommonSysImc_SocketPair(struct NaClAppThread *natp,
                                    int32_t              *d_out) {
  uintptr_t               sysaddr;
  struct NaClDesc         *pair[2];
  int32_t                 retval;

  NaClSysCommonThreadSyscallEnter(natp);

  sysaddr = NaClUserToSysAddrRange(natp->nap,
                                   (uintptr_t) d_out,
                                   2 * sizeof *d_out);
  if (kNaClBadAddress == sysaddr) {
    NaClLog(1,
            ("NaClCommonSysImc_Socket_Pair: bad output descriptor array "
             " (0x%08"PRIxPTR")\n"),
            (uintptr_t) d_out);
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }

  d_out = (int *) sysaddr;
  retval = NaClCommonDescSocketPair(pair);
  if (0 != retval) {
    goto cleanup;
  }

  d_out[0] = NaClSetAvail(natp->nap, pair[0]);
  d_out[1] = NaClSetAvail(natp->nap, pair[1]);

  retval = 0;
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

int32_t NaClCommonSysTls_Init(struct NaClAppThread  *natp,
                              void                  *tdb,
                              size_t                tdb_size) {
  int32_t   retval = -NACL_ABI_EINVAL;
  uintptr_t sys_tdb;

  NaClSysCommonThreadSyscallEnter(natp);

  /* Verify that the address in the app's range and translated from
   * nacl module address to service runtime address - a nop on ARM
   */
  sys_tdb = NaClUserToSysAddrRange(natp->nap, (uintptr_t) tdb, tdb_size);
  if (kNaClBadAddress == sys_tdb) {
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }

  if (0 == NaClTlsChange(natp, (void *)sys_tdb, tdb_size)) {
    retval = -NACL_ABI_EINVAL;
    goto cleanup;
  }
  retval = 0;
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

int32_t NaClCommonSysThread_Create(struct NaClAppThread *natp,
                                   void                 *prog_ctr,
                                   void                 *stack_ptr,
                                   void                 *tdb,
                                   size_t               tdb_size) {
  int32_t     retval = -NACL_ABI_EINVAL;
  uintptr_t   sys_tdb;

  NaClSysCommonThreadSyscallEnter(natp);

  /* make sure that the thread start function is in the text region */
  if ((uintptr_t) prog_ctr >= NACL_TRAMPOLINE_END +
                              natp->nap->text_region_bytes) {
    NaClLog(LOG_ERROR, "bad pc start\n");
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }
  /* make sure that the thread start function is aligned */
  /* TODO(robertm): there should be a function for this test */
#if !defined(DANGEROUS_DEBUG_MODE_DISABLE_INNER_SANDBOX)

  if (0 != ((natp->nap->align_boundary - 1) & (uintptr_t) prog_ctr)) {
    NaClLog(LOG_ERROR, "bad pc alignment\n");
    retval = -NACL_ABI_EINVAL;
    goto cleanup;
  }
#endif
  /* we do not enforce stack alignment */
  if (kNaClBadAddress == NaClUserToSysAddr(natp->nap, (uintptr_t) stack_ptr)) {
    NaClLog(LOG_ERROR, "bad stack\n");
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }
  sys_tdb = NaClUserToSysAddrRange(natp->nap, (uintptr_t) tdb, tdb_size);
  if (kNaClBadAddress == sys_tdb) {
    NaClLog(LOG_ERROR, "bad tdb\n");
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }

  retval = NaClCreateAdditionalThread(natp->nap,
                                      (uintptr_t) prog_ctr,
                                      (uintptr_t) stack_ptr,
                                      sys_tdb,
                                      tdb_size);
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

int NaClCommonSysThread_Nice(struct NaClAppThread *natp,
                             const int            nice) {
  /* Note: implementation of nacl_thread_nice is OS dependent. */
  UNREFERENCED_PARAMETER(natp);
  return nacl_thread_nice(nice);
}

#if defined(HAVE_SDL)


int32_t NaClCommonSysMultimedia_Init(struct NaClAppThread *natp,
                                     int                  subsys) {
  int32_t retval = -NACL_ABI_EINVAL;
  /* for 64-bit mode, cast to pointer from integer of different
   * size */
  uintptr_t subsys_arg = subsys;

  NaClLog(3, "NaClBotSysMultimedia_Init is 0x%08"PRIxPTR"\n",
          (uintptr_t) NaClBotSysMultimedia_Init);
  NaClLog(3, "NaClClosure2Run_Init is 0x%08"PRIxPTR"\n",
          (uintptr_t) NaClClosure2Run);

  NaClSysCommonThreadSyscallEnter(natp);

  if (!natp->is_privileged && natp->nap->restrict_to_main_thread) {
    retval = -NACL_ABI_EIO;
    goto cleanup;
  }
  NaClStartAsyncOp(natp,
                   ((struct NaClClosure *)
                    NaClClosure2Ctor(((void (*)(void *, void *))
                                      NaClBotSysMultimedia_Init),
                                     (void *) natp,
                                     (void *) subsys_arg)));
  retval = NaClWaitForAsyncOp(natp);
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}


int32_t NaClCommonSysMultimedia_Shutdown(struct NaClAppThread *natp) {
  int32_t retval = -NACL_ABI_EINVAL;

  NaClLog(3, "NaClBotSysMultimedia_Shudown is 0x%08"PRIxPTR"\n",
          (uintptr_t) NaClBotSysMultimedia_Shutdown);
  NaClLog(3, "NaClClosure1Run_Init is 0x%08"PRIxPTR"\n",
          (uintptr_t) NaClClosure1Run);

  NaClSysCommonThreadSyscallEnter(natp);

  if (!natp->is_privileged && natp->nap->restrict_to_main_thread) {
    retval = -NACL_ABI_EIO;
    goto cleanup;
  }

  NaClStartAsyncOp(natp,
                   ((struct NaClClosure *)
                    NaClClosure1Ctor(((void (*)(void *))
                                      NaClBotSysMultimedia_Shutdown),
                                     (void *) natp)));
  retval = NaClWaitForAsyncOp(natp);
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}


int32_t NaClCommonSysVideo_Init(struct NaClAppThread *natp,
                                int                  width,
                                int                  height) {
  int32_t retval = -NACL_ABI_EINVAL;
  /* for 64-bit mode, cast to pointer from integer of different
   * size */
  uintptr_t width_arg = width;
  uintptr_t height_arg = height;

  NaClSysCommonThreadSyscallEnter(natp);

  /* privileged syscall: forward to main thread */
  if (!natp->is_privileged && natp->nap->restrict_to_main_thread) {
    retval = -NACL_ABI_EIO;
    goto cleanup;
  }

  /*  make sure width & height are not unreasonable */
  if ((width < kNaClVideoMinWindowSize) ||
      (width > kNaClVideoMaxWindowSize) ||
      (height < kNaClVideoMinWindowSize) ||
      (height > kNaClVideoMaxWindowSize)) {
    NaClLog(LOG_ERROR, "NaClSysVideo_Init: invalid window size!\n");
    retval = -NACL_ABI_EINVAL;
    goto cleanup;
  }

  /* width and height must also be multiples of 4 */
  if ((0 != (width & 0x3)) || (0 != (height & 0x3))) {
    NaClLog(LOG_ERROR,
            "NaClSysVideo_Init: width & height must be a multiple of 4!\n");
    retval = -NACL_ABI_EINVAL;
    goto cleanup;
  }

  NaClLog(3, "NaClBotSysVideo_Init is 0x%08"PRIxPTR"\n",
          (uintptr_t) NaClBotSysVideo_Init);
  NaClLog(3, "NaClClosure4Run_Init is 0x%08"PRIxPTR"\n",
          (uintptr_t) NaClClosure4Run);
  NaClStartAsyncOp(natp,
                 ((struct NaClClosure *)
                  NaClClosure3Ctor(((void (*)(void *, void *, void *))
                                     NaClBotSysVideo_Init),
                                   (void *) natp,
                                   (void *) width_arg,
                                   (void *) height_arg)));
  retval = NaClWaitForAsyncOp(natp);
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

int32_t NaClCommonSysVideo_Shutdown(struct NaClAppThread *natp) {
  int32_t retval;

  NaClSysCommonThreadSyscallEnter(natp);

  if (!natp->is_privileged && natp->nap->restrict_to_main_thread) {
    retval = -NACL_ABI_EIO;
    goto cleanup;
  }

  NaClStartAsyncOp(natp,
                   ((struct NaClClosure *)
                    NaClClosure1Ctor(((void (*)(void *))
                                      NaClBotSysVideo_Shutdown),
                                     (void *) natp)));
  retval = NaClWaitForAsyncOp(natp);
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}


int32_t NaClCommonSysVideo_Update(struct NaClAppThread *natp,
                                  const void           *data) {
  int32_t retval = -NACL_ABI_EINVAL;

  NaClSysCommonThreadSyscallEnter(natp);

  if (NULL == data) {
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }
  /*
   * further validation of data argument is deferred until bottom half
   * (the size of the data is not available from here)
   */
  NaClStartAsyncOp(natp,
                   ((struct NaClClosure *)
                    NaClClosure2Ctor(((void (*)(void *, void *))
                                       NaClBotSysVideo_Update),
                                      (void *) natp,
                                      (void *) data)));
  retval = NaClWaitForAsyncOp(natp);
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}


int32_t NaClCommonSysVideo_Poll_Event(struct NaClAppThread      *natp,
                                      union NaClMultimediaEvent *event) {
  int32_t   retval = -NACL_ABI_EINVAL;
  uintptr_t sysaddr;

  NaClLog(3, "NaClBotSysVideo_Poll_Event is 0x%08"PRIxPTR"\n",
          (uintptr_t) NaClBotSysVideo_Poll_Event);
  NaClLog(3, "NaClClosure2Run_Init is 0x%08"PRIxPTR"\n",
          (uintptr_t) NaClClosure2Run);

  NaClSysCommonThreadSyscallEnter(natp);

  if (!natp->is_privileged && natp->nap->restrict_to_main_thread) {
    retval = -NACL_ABI_EIO;
    goto cleanup;
  }
  sysaddr = NaClUserToSysAddrRange(natp->nap,
                                  (uintptr_t) event,
                                  sizeof(*event));
  if (kNaClBadAddress == sysaddr) {
    NaClLog(1, "NaClCommonSysVideo_Poll_Event: data address invalid\n");
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }
  NaClStartAsyncOp(natp,
                   ((struct NaClClosure *)
                    NaClClosure2Ctor(((void (*)(void *, void *))
                                      NaClBotSysVideo_Poll_Event),
                                     (void *) natp,
                                     (void *) sysaddr)));
  retval = NaClWaitForAsyncOp(natp);
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}


int32_t NaClCommonSysAudio_Init(struct NaClAppThread  *natp,
                                enum NaClAudioFormat  format,
                                int                   desired_samples,
                                int                   *obtained_samples) {
  int32_t   retval = -NACL_ABI_EINVAL;
  uintptr_t sysaddr;
  /* for 64-bit mode, cast to pointer from integer of different
   * size */
  uintptr_t desired_samples_arg = desired_samples;

  NaClLog(3, "NaClBotSysAudio_Init is 0x%08"PRIxPTR"\n",
          (uintptr_t) NaClBotSysAudio_Init);
  NaClLog(3, "NaClClosure4Run_Init is 0x%08"PRIxPTR"\n",
          (uintptr_t) NaClClosure4Run);

  NaClSysCommonThreadSyscallEnter(natp);

  if (!natp->is_privileged && natp->nap->restrict_to_main_thread) {
    retval = -NACL_ABI_EIO;
    goto cleanup;
  }

  sysaddr = NaClUserToSysAddrRange(natp->nap,
                                   (uintptr_t) obtained_samples,
                                   sizeof(*obtained_samples));
  if (kNaClBadAddress == sysaddr) {
    NaClLog(1, "NaClCommonSysAudio_Init: input address invalid\n");
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }
  NaClStartAsyncOp(natp,
                   ((struct NaClClosure *)
                    NaClClosure4Ctor(((void (*)(void *, void *,
                                                void *, void *))
                                      NaClBotSysAudio_Init),
                                     (void *) natp,
                                     (void *) format,
                                     (void *) desired_samples_arg,
                                     (void *) sysaddr)));
  retval = NaClWaitForAsyncOp(natp);
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}


int32_t NaClCommonSysAudio_Shutdown(struct NaClAppThread *natp) {
  int32_t retval = -NACL_ABI_EINVAL;

  NaClLog(3, "NaClBotSysAudio_Shutdown is 0x%08"PRIxPTR"\n",
          (uintptr_t) NaClBotSysAudio_Shutdown);
  NaClLog(3, "NaClClosure1Run_Init is 0x%08"PRIxPTR"\n",
          (uintptr_t) NaClClosure1Run);

  NaClSysCommonThreadSyscallEnter(natp);

  if (!natp->is_privileged && natp->nap->restrict_to_main_thread) {
    retval = -NACL_ABI_EIO;
    goto cleanup;
  }

  NaClStartAsyncOp(natp,
                   ((struct NaClClosure *)
                    NaClClosure1Ctor(((void (*)(void *))
                                      NaClBotSysAudio_Shutdown),
                                     (void *) natp)));
  retval = NaClWaitForAsyncOp(natp);
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

#endif

int32_t NaClCommonSysMutex_Create(struct NaClAppThread *natp) {
  int32_t              retval = -NACL_ABI_EINVAL;
  struct NaClDescMutex *desc;

  NaClSysCommonThreadSyscallEnter(natp);

  desc = malloc(sizeof(*desc));

  if (!desc || !NaClDescMutexCtor(desc)) {
    retval = -NACL_ABI_ENOMEM;
    goto cleanup;
  }

  retval = NaClSetAvail(natp->nap, (struct NaClDesc *)desc);
  desc = NULL;
cleanup:
  free(desc);
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

int32_t NaClCommonSysMutex_Lock(struct NaClAppThread  *natp,
                                   int32_t            mutex_handle) {
  int32_t               retval = -NACL_ABI_EINVAL;
  struct NaClDesc       *desc;

  NaClSysCommonThreadSyscallEnter(natp);

  desc = NaClGetDesc(natp->nap, mutex_handle);

  if (NULL == desc) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  retval = (*desc->vtbl->Lock)(desc, natp->effp);
  NaClDescUnref(desc);

cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

int32_t NaClCommonSysMutex_Unlock(struct NaClAppThread  *natp,
                                  int32_t               mutex_handle) {
  int32_t         retval = -NACL_ABI_EINVAL;
  struct NaClDesc *desc;

  NaClSysCommonThreadSyscallEnter(natp);

  desc = NaClGetDesc(natp->nap, mutex_handle);

  if (NULL == desc) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  retval = (*desc->vtbl->Unlock)(desc, natp->effp);
  NaClDescUnref(desc);

cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

int32_t NaClCommonSysMutex_Trylock(struct NaClAppThread   *natp,
                                  int32_t                 mutex_handle) {
  int32_t         retval = -NACL_ABI_EINVAL;
  struct NaClDesc *desc;

  NaClSysCommonThreadSyscallEnter(natp);

  desc = NaClGetDesc(natp->nap, mutex_handle);

  if (NULL == desc) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  retval = (*desc->vtbl->TryLock)(desc, natp->effp);
  NaClDescUnref(desc);

cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

int32_t NaClCommonSysCond_Create(struct NaClAppThread *natp) {
  int32_t                retval = -NACL_ABI_EINVAL;
  struct NaClDescCondVar *desc;

  NaClSysCommonThreadSyscallEnter(natp);

  desc = malloc(sizeof(*desc));

  if (!desc || !NaClDescCondVarCtor(desc)) {
    retval = -NACL_ABI_ENOMEM;
    goto cleanup;
  }

  retval = NaClSetAvail(natp->nap, (struct NaClDesc *)desc);
  desc = NULL;
cleanup:
  free(desc);
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

int32_t NaClCommonSysCond_Wait(struct NaClAppThread *natp,
                               int32_t              cond_handle,
                               int32_t              mutex_handle) {
  int32_t         retval = -NACL_ABI_EINVAL;
  struct NaClDesc *cv_desc;
  struct NaClDesc *mutex_desc;

  NaClSysCommonThreadSyscallEnter(natp);

  cv_desc = NaClGetDesc(natp->nap, cond_handle);

  if (NULL == cv_desc) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  mutex_desc = NaClGetDesc(natp->nap, mutex_handle);
  if (NULL == mutex_desc) {
    NaClDescUnref(cv_desc);
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  retval = (*cv_desc->vtbl->Wait)(cv_desc, natp->effp, mutex_desc);
  NaClDescUnref(cv_desc);
  NaClDescUnref(mutex_desc);

cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

int32_t NaClCommonSysCond_Signal(struct NaClAppThread *natp,
                                 int32_t              cond_handle) {
  int32_t         retval = -NACL_ABI_EINVAL;
  struct NaClDesc *desc;

  NaClSysCommonThreadSyscallEnter(natp);

  desc = NaClGetDesc(natp->nap, cond_handle);

  if (NULL == desc) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  retval = (*desc->vtbl->Signal)(desc, natp->effp);
  NaClDescUnref(desc);
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

int32_t NaClCommonSysCond_Broadcast(struct NaClAppThread  *natp,
                                    int32_t               cond_handle) {
  struct NaClDesc *desc;
  int32_t         retval = -NACL_ABI_EINVAL;

  NaClSysCommonThreadSyscallEnter(natp);

  desc = NaClGetDesc(natp->nap, cond_handle);

  if (NULL == desc) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  retval = (*desc->vtbl->Broadcast)(desc, natp->effp);
  NaClDescUnref(desc);

cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

int32_t NaClCommonSysCond_Timed_Wait_Abs(struct NaClAppThread     *natp,
                                         int32_t                  cond_handle,
                                         int32_t                  mutex_handle,
                                         struct nacl_abi_timespec *ts) {
  int32_t                  retval = -NACL_ABI_EINVAL;
  struct NaClDesc          *cv_desc;
  struct NaClDesc          *mutex_desc;
  uintptr_t                sys_ts;
  struct nacl_abi_timespec trusted_ts;

  NaClSysCommonThreadSyscallEnter(natp);

  sys_ts = NaClUserToSysAddrRange(natp->nap,
                                  (uintptr_t) ts,
                                  sizeof(*ts));
  if (kNaClBadAddress == sys_ts) {
    retval = -NACL_ABI_EFAULT;
    goto cleanup;
  }
  /* TODO(gregoryd): validate ts - do we have a limit for time to wait? */
  memcpy(&trusted_ts, (void*) sys_ts, sizeof(trusted_ts));

  cv_desc = NaClGetDesc(natp->nap, cond_handle);
  if (NULL == cv_desc) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  mutex_desc = NaClGetDesc(natp->nap, mutex_handle);
  if (NULL == mutex_desc) {
    NaClDescUnref(cv_desc);
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  retval = (*cv_desc->vtbl->TimedWaitAbs)(cv_desc, natp->effp, mutex_desc,
                                          &trusted_ts);
  NaClDescUnref(cv_desc);
  NaClDescUnref(mutex_desc);
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

int32_t NaClCommonSysSem_Create(struct NaClAppThread *natp,
                                int32_t              init_value) {
  int32_t                  retval = -NACL_ABI_EINVAL;
  struct NaClDescSemaphore *desc;

  NaClSysCommonThreadSyscallEnter(natp);

  desc = malloc(sizeof(*desc));

  if (!desc || !NaClDescSemaphoreCtor(desc, init_value)) {
    retval = -NACL_ABI_ENOMEM;
    goto cleanup;
  }

  retval = NaClSetAvail(natp->nap, (struct NaClDesc *) desc);
  desc = NULL;
cleanup:
  free(desc);
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}


int32_t NaClCommonSysSem_Wait(struct NaClAppThread *natp,
                              int32_t              sem_handle) {
  int32_t         retval = -NACL_ABI_EINVAL;
  struct NaClDesc *desc;

  NaClSysCommonThreadSyscallEnter(natp);

  desc = NaClGetDesc(natp->nap, sem_handle);

  if (NULL == desc) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  /*
   * TODO(gregoryd): we have to decide on the syscall API: do we
   * switch to read/write/ioctl API or do we stay with the more
   * detailed API. Anyway, using a single syscall for waiting on all
   * synchronization objects makes sense.
   */
  retval = (*desc->vtbl->SemWait)(desc, natp->effp);
  NaClDescUnref(desc);
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

int32_t NaClCommonSysSem_Post(struct NaClAppThread *natp,
                              int32_t              sem_handle) {
  int32_t         retval = -NACL_ABI_EINVAL;
  struct NaClDesc *desc;

  NaClSysCommonThreadSyscallEnter(natp);

  desc = NaClGetDesc(natp->nap, sem_handle);

  if (NULL == desc) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  retval = (*desc->vtbl->Post)(desc, natp->effp);
  NaClDescUnref(desc);
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}

int32_t NaClCommonSysSem_Get_Value(struct NaClAppThread *natp,
                                   int32_t              sem_handle) {
  int32_t         retval = -NACL_ABI_EINVAL;
  struct NaClDesc *desc;

  NaClSysCommonThreadSyscallEnter(natp);

  desc = NaClGetDesc(natp->nap, sem_handle);

  if (NULL == desc) {
    retval = -NACL_ABI_EBADF;
    goto cleanup;
  }

  retval = (*desc->vtbl->GetValue)(desc, natp->effp);
  NaClDescUnref(desc);
cleanup:
  NaClSysCommonThreadSyscallLeave(natp);
  return retval;
}
