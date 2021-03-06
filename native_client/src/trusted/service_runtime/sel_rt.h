/*
 * Copyright 2009, Google Inc.
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

#ifndef NATIVE_CLIENT_SERVICE_RUNTIME_SEL_RT_H__
#define NATIVE_CLIENT_SERVICE_RUNTIME_SEL_RT_H__ 1

#include "native_client/src/include/nacl_base.h"

#if NACL_ARCH(NACL_BUILD_ARCH) == NACL_x86
#include "native_client/src/trusted/service_runtime/arch/x86/sel_rt.h"
#elif NACL_ARCH(NACL_BUILD_ARCH) == NACL_arm
#include "native_client/src/trusted/service_runtime/arch/arm/sel_rt.h"
#else
#error Unknown platform!
#endif

struct NaClApp; /* fwd */

void      NaClInitGlobals();

uintptr_t NaClGetThreadCtxSp(struct NaClThreadContext  *th_ctx);

void      NaClSetThreadCtxSp(struct NaClThreadContext  *th_ctx, uintptr_t sp);

#endif  /* NATIVE_CLIENT_SERVICE_RUNTIME_SEL_RT_H__ */

