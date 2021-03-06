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
 * ncvalidate.c
 * Validate x86 instructions for Native Client
 *
 */
#include "native_client/src/include/portability.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include "native_client/src/trusted/validator_x86/ncdecode.h"
#include "native_client/src/trusted/validator_x86/ncvalidate_internaltypes.h"
#include "native_client/src/trusted/validator_x86/nacl_cpuid.h"
#include "native_client/src/trusted/service_runtime/sel_ldr.h"

/* debugging stuff */
#define DEBUGGING 0
#if DEBUGGING
#define dprint(args)        do { printf args; } while (0)
#else
#define dprint(args)        do { if (0) { printf args; } } while (0)
/* allows DCE but compiler can still do format string checks */
#endif  /* DEBUGGING */

/* TODO(bradchen) verbosity needs to be controllable via commandline flags */
#define VERBOSE 1
#if VERBOSE
#define vprint(args)        do { printf args; } while (0)
#else
#define vprint(args)        do { if (0) { printf args; } } while (0)
/* allows DCE but compiler can still do format string checks */
#endif  /* VERBOSE */

/* g_print_diagnostics is used to produce or surpress validator error */
/* messages. The messages tend to be confusing when generated by      */
/* sel_ldr, as the instruction addresses don't correspond to the      */
/* addresses in disassembler output. This flag is used to surpress    */
/* validator error details from sel_ldr, and allow details from       */
/* ncval, which produces messages with the expected addresses.        */
static int g_print_diagnostics = 1;
static void ValidatePrintError(const PcAddress addr, char *msg) {
  if (g_print_diagnostics != 1) return;
  printf("VALIDATOR: %"PRIxPcAddress": %s\n", addr, msg);
}

/* opcode histogram */
#if VERBOSE == 1
static void OpcodeHisto(const uint8_t byte1, struct NCValidatorState *vstate) {
  vstate->opcodehisto[byte1] += 1;
}

static void InitOpcodeHisto(struct NCValidatorState *vstate) {
  int i;
  for (i = 0; i < 256; i += 1) vstate->opcodehisto[i] = 0;
}

static void PrintOpcodeHisto(FILE *f, struct NCValidatorState *vstate) {
  int i;
  int printed_in_this_row = 0;
  if (!VERBOSE) return;
  fprintf(f, "\nOpcode Histogram;\n");
  for (i = 0; i < 256; ++i) {
    if (0 != vstate->opcodehisto[i]) {
      fprintf(f, "%d\t0x%02x\t", vstate->opcodehisto[i], i);
      ++printed_in_this_row;
      if (printed_in_this_row > 3) {
        printed_in_this_row = 0;
        fprintf(f, "\n");
      }
    }
  }
  if (0 != printed_in_this_row) {
    fprintf(f, "\n");
  }
}
#else
#define OpcodeHisto(b, v)
#define InitOpcodeHisto(v)
#define PrintOpcodeHisto(f, v)
#endif /* VERBOSE == 1 */

/* statistics code */
static void Stats_Inst(struct NCValidatorState *vstate) {
  vstate->stats.instructions += 1;
}

static void Stats_CheckTarget(struct NCValidatorState *vstate) {
  vstate->stats.checktarget += 1;
}

static void Stats_TargetIndirect(struct NCValidatorState *vstate) {
  vstate->stats.targetindirect += 1;
}

static void Stats_SawFailure(struct NCValidatorState *vstate) {
  vstate->stats.sawfailure = 1;
}

static void Stats_InternalError(struct NCValidatorState *vstate) {
  vstate->stats.internalerrors += 1;
  Stats_SawFailure(vstate);
}

static void Stats_BadCPU(struct NCValidatorState *vstate) {
  vstate->stats.badcpu += 1;
  Stats_SawFailure(vstate);
}

static void Stats_BadAlignment(struct NCValidatorState *vstate) {
  vstate->stats.badalignment += 1;
  Stats_SawFailure(vstate);
}

static void Stats_SegFault(struct NCValidatorState *vstate) {
  vstate->stats.segfaults += 1;
  Stats_SawFailure(vstate);
}

static void Stats_NewSegment(struct NCValidatorState *vstate) {
  vstate->stats.segments += 1;
  if (vstate->stats.segments > 1) {
    vprint(("error: multiple segments\n"));
    Stats_SawFailure(vstate);
  }
}

static void Stats_BadTarget(struct NCValidatorState *vstate) {
  vstate->stats.badtarget += 1;
  Stats_SawFailure(vstate);
}

static void Stats_UnsafeIndirect(struct NCValidatorState *vstate) {
  vstate->stats.unsafeindirect += 1;
  Stats_SawFailure(vstate);
}

static void Stats_MissingFullStop(struct NCValidatorState *vstate) {
  vstate->stats.missingfullstop += 1;
  Stats_SawFailure(vstate);
}

static void Stats_Return(struct NCValidatorState *vstate) {
  vstate->stats.returns += 1;
  Stats_UnsafeIndirect(vstate);
  Stats_SawFailure(vstate);
}

static void Stats_IllegalInst(struct NCValidatorState *vstate) {
  vstate->stats.illegalinst += 1;
  Stats_SawFailure(vstate);
}

static void Stats_BadPrefix(struct NCValidatorState *vstate) {
  vstate->stats.badprefix += 1;
  vstate->stats.illegalinst += 1; /* a bad prefix is also an invalid inst */
  Stats_SawFailure(vstate);
}

static void Stats_BadInstLength(struct NCValidatorState *vstate) {
  vstate->stats.badinstlength += 1;
  Stats_SawFailure(vstate);
}

static void Stats_Init(struct NCValidatorState *vstate) {
  static int inited = 0;
  if (inited) return;

  vstate->stats.instructions = 0;
  vstate->stats.segments = 0;
  vstate->stats.checktarget = 0;
  vstate->stats.targetindirect = 0;
  vstate->stats.badtarget = 0;
  vstate->stats.unsafeindirect = 0;
  vstate->stats.returns = 0;
  vstate->stats.illegalinst = 0;
  vstate->stats.badalignment = 0;
  vstate->stats.internalerrors = 0;
  vstate->stats.badcpu = 0;
  vstate->stats.missingfullstop = 0;
  vstate->stats.badinstlength = 0;
  vstate->stats.badprefix = 0;
  vstate->stats.sawfailure = 0;
  InitOpcodeHisto(vstate);
  inited = 1;
}

void Stats_Print(FILE *f, struct NCValidatorState *vstate) {
  if (!VERBOSE) return;
  if (vstate == NULL) {
    fprintf(f, "Analysis Summary: invalid module or internal failure\n");
    return;
  }
  PrintOpcodeHisto(f, vstate);
  fprintf(f, "Analysis Summary:\n");
  fprintf(f, "%d Checked instructions\n", vstate->stats.instructions);
  fprintf(f, "%d checked jump targets\n", vstate->stats.checktarget);
  fprintf(f, "%d calls/jumps need dynamic checking (%0.2f%%)\n",
          vstate->stats.targetindirect,
          vstate->stats.instructions ?
          100.0 * vstate->stats.targetindirect/vstate->stats.instructions : 0);
  fprintf(f, "\nProblems:\n");
  fprintf(f, "%d illegal instructions\n", vstate->stats.illegalinst);
  fprintf(f, "%d bad jump targets\n", vstate->stats.badtarget);
  fprintf(f, "%d illegal unprotected indirect jumps (including ret)\n",
          vstate->stats.unsafeindirect);
  fprintf(f, "%d instruction alignment defects\n",
          vstate->stats.badalignment);
  fprintf(f, "%d segmentation errors\n",
          vstate->stats.segfaults);
  fprintf(f, "%d bad prefix\n",
          vstate->stats.badprefix);
  fprintf(f, "%d bad instruction length\n",
          vstate->stats.badinstlength);
  fprintf(f, "%d missing full stop\n",
          vstate->stats.missingfullstop);
  fprintf(f, "%d internal errors\n",
          vstate->stats.internalerrors);
  fprintf(f, "%d bad cpu\n",
          vstate->stats.badcpu);
}

/***********************************************************************/
/* jump target table                                                   */
static const uint8_t iadrmasks[8] = {0x01, 0x02, 0x04, 0x08,
                                     0x10, 0x20, 0x40, 0x80};
#define IATOffset(__IA) ((__IA) >> 3)
#define IATMask(__IA) (iadrmasks[(__IA) & 0x7])
#define SetAdrTable(__IOFF, __TABLE) \
  (__TABLE)[IATOffset(__IOFF)] |= IATMask(__IOFF)
#define ClearAdrTable(__IOFF, __TABLE) \
  (__TABLE)[IATOffset(__IOFF)] &= ~(IATMask(__IOFF))
#define GetAdrTable(__IOFF, __TABLE) \
  ((__TABLE)[IATOffset(__IOFF)] & IATMask(__IOFF))

/* forward declaration, for registration */
void ValidateInst(const struct NCDecoderState *mstate);

/* In general we are quite paranoid about what prefixes can     */
/* be used and where. For one-byte opcodes, prefixes are        */
/* restricted based on the NACLi_ type and the masks in         */
/* BadPrefixMask. For two-byte opcodes, any                     */
/* prefix can be used, but they function more to define the     */
/* opcode as opposed to modify it; hence there are separate     */
/* tables in ncdecodetab.h for the four allowed prefix bytes.   */
static uint32_t BadPrefixMask[kNaClInstTypeRange];
static void InitBadPrefixMask() {
  int i;

  for (i = 0; i < kNaClInstTypeRange; i++) {
    BadPrefixMask[i] = 0xffffffff;  /* all prefixes are bad */
  }
  BadPrefixMask[NACLi_386]   = ~(kPrefixDATA16 | kPrefixSEGGS);
  BadPrefixMask[NACLi_386L]  = ~(kPrefixDATA16 | kPrefixLOCK);
  BadPrefixMask[NACLi_386R]  = ~(kPrefixDATA16 | kPrefixREP);
  BadPrefixMask[NACLi_386RE] = ~(kPrefixDATA16 | kPrefixREP | kPrefixREPNE);
  /* CS and DS prefix bytes are used as branch prediction hints  */
  /* and do not affect target address computation.               */
  BadPrefixMask[NACLi_JMP8]  = ~(kPrefixSEGCS | kPrefixSEGDS);
  BadPrefixMask[NACLi_JMPZ]  = ~(kPrefixSEGCS | kPrefixSEGDS);
  BadPrefixMask[NACLi_CMPXCHG8B] = ~kPrefixLOCK;
}

/*
 * NCValidateInit: Initialize NaCl validator internal state
 * Parameters:
 *    vbase: base virtual address for code segment
 *    vlimit: size in bytes of code segment
 *    alignment: 16 or 32, specifying alignment
 * Returns:
 *    an initialized struct NCValidatorState * if everything is okay,
 *    else NULL
 */
struct NCValidatorState *NCValidateInit(const uint32_t vbase,
                                        const uint32_t vlimit,
                                        const uint8_t alignment) {
  uint32_t alignbase = vbase & (~alignment);
  struct NCValidatorState *vstate;

  dprint(("NCValidateInit(%08x, %08x, %08x)\n", vbase, vlimit, alignment));
  InitBadPrefixMask();
  do {
    if (vlimit <= vbase) break;
    if (alignment != 16 && alignment != 32) break;
    dprint(("ncv_init(%x, %x)\n", vbase, vlimit));
    vstate = (struct NCValidatorState *)calloc(1, sizeof(*vstate));
    if (vstate == NULL) break;
    vstate->iadrbase = alignbase;
    vstate->iadrlimit = vlimit;
    vstate->alignment = alignment;
    vstate->alignmask = alignment-1;
    vstate->vttable = (uint8_t *)calloc(IATOffset(vlimit - alignbase) + 1, 1);
    vstate->kttable = (uint8_t *)calloc(IATOffset(vlimit - alignbase) + 1, 1);
    if (vstate->vttable == NULL || vstate->kttable == NULL) break;
    dprint(("  allocated tables\n"));
    Stats_Init(vstate);
    NCDecodeRegisterCallbacks(ValidateInst, Stats_NewSegment,
                              Stats_SegFault, Stats_InternalError);
    return vstate;
  } while (0);
  /* failure */
  return NULL;
}

static void RememberIP(const PcAddress ip, struct NCValidatorState *vstate) {
  const MemorySize ioffset =  ip - vstate->iadrbase;
  if (ip < vstate->iadrbase || ip >= vstate->iadrlimit) {
    ValidatePrintError(ip, "JUMP TARGET out of range in RememberIP");
    Stats_BadTarget(vstate);
    return;
  }
  if (GetAdrTable(ioffset, vstate->vttable)) {
    vprint(("RememberIP: Saw inst at %"PRIxPcAddressAll" twice\n", ip));
    Stats_InternalError(vstate);
    return;
  }
  Stats_Inst(vstate);
  SetAdrTable(ioffset, vstate->vttable);
}

static void RememberTP(const PcAddress src, PcAddress target,
                       struct NCValidatorState *vstate) {
  const MemorySize ioffset =  target - vstate->iadrbase;

  do {
    if (target < vstate->iadrlimit) {
      if (target >= vstate->iadrbase) break;
      /*
       * the trampolines need to be aligned 0mod32 regardless of the
       * program's elf flags.  This allows the same library to be used
       * with both 16 and 32 byte aligned clients.
       */
      if (target >= vstate->iadrbase - NACL_TRAMPOLINE_END
          && ((target & (NACL_INSTR_BLOCK_SIZE-1)) == 0)) {
        /*
         * TODO(sehr): once we fully support 16/32 alignment, remove this
         * in favor of however we communicate the fixed block size.
         */
        /* this is an aligned target in the trampoline area; ignore */
        /* vprint(("ignoring target %08x in trampoline\n", target)); */
        return;
      }
    }
    ValidatePrintError(src, "JUMP TARGET out of range");
    Stats_BadTarget(vstate);
    return;
  } while (0);
  SetAdrTable(ioffset, vstate->kttable);
}

static void ForgetIP(const PcAddress ip,
                     struct NCValidatorState *vstate) {
  MemorySize ioffset =  ip - vstate->iadrbase;
  if (ip < vstate->iadrbase || ip >= vstate->iadrlimit) {
    ValidatePrintError(ip, "JUMP TARGET out of range in ForgetIP");
    Stats_BadTarget(vstate);
    return;
  }
  ClearAdrTable(ioffset, vstate->vttable);
}

int NCValidateFinish(struct NCValidatorState *vstate) {
  uint32_t offset;
  if (vstate == NULL) {
    vprint(("validator not initialized. Did you call ncvalidate_init()?\n"));
    /* non-zero indicates failure */
    return 1;
  }
  dprint(("CheckTargets: %x-%x\n", vstate->iadrbase, vstate->iadrlimit));
  for (offset = 0;
       offset < vstate->iadrlimit - vstate->iadrbase;
       offset += 1) {
    if (GetAdrTable(offset, vstate->kttable)) {
      /* printf("CheckTarget %x\n", offset + iadrbase); */
      Stats_CheckTarget(vstate);
      if (!GetAdrTable(offset, vstate->vttable)) {
        ValidatePrintError(vstate->iadrbase + offset, "Bad jump target");
        Stats_BadTarget(vstate);
      }
    }
  }
  /* check basic block boundaries */
  if (vstate->iadrbase & vstate->alignmask) {
    ValidatePrintError(vstate->iadrbase, "Bad base address alignment");
    Stats_BadAlignment(vstate);
  }
  for (offset = 0; offset < vstate->iadrlimit - vstate->iadrbase;
       offset += vstate->alignment) {
    if (!GetAdrTable(offset, vstate->vttable)) {
      ValidatePrintError(vstate->iadrbase + offset, "Bad basic block alignent");
      Stats_BadAlignment(vstate);
    }
  }
  fflush(stdout);

  /* Now that all the work is done, generate return code. */
  /* Return zero if there are no problems.                */
  return (vstate->stats.sawfailure);
}

void NCValidateFreeState(struct NCValidatorState **vstate) {
  if (*vstate == NULL) return;
  free((*vstate)->vttable);
  free((*vstate)->kttable);
  free(*vstate);
  *vstate = NULL;
}

/* ValidateSFenceClFlush is called for the sfence/clflush opcode 0f ae /7 */
/* It returns 0 if the current instruction is implemented, and 1 if not.  */
static int ValidateSFenceClFlush(const struct NCDecoderState *mstate) {
  uint8_t mrm = (uint8_t)mstate->inst.maddr[2];

  if (modrm_mod(mrm) == 3) {
    /* this is an sfence */
    if (mstate->vstate->cpufeatures.f_FXSR) return 0;
    return 1;
  } else {
    /* this is an clflush */
    if (mstate->vstate->cpufeatures.f_CLFLUSH) return 0;
    return 1;
  }
}

static void ValidateCallAlignment(const struct NCDecoderState *mstate) {
  PcAddress fallthru = mstate->inst.vaddr + mstate->inst.length;
  if (fallthru & mstate->vstate->alignmask) {
    ValidatePrintError(mstate->inst.vaddr, "Bad call alignment");
    /* This makes bad call alignment a fatal error. */
    Stats_BadAlignment(mstate->vstate);
  }
}

static void ValidateJmp8(const struct NCDecoderState *mstate) {
  uint8_t opcode = (uint8_t)mstate->inst.maddr[mstate->inst.prefixbytes];
  int8_t offset = (int8_t)mstate->inst.maddr[mstate->inst.prefixbytes+1];
  PcAddress target = mstate->inst.vaddr + mstate->inst.length + offset;
  Stats_CheckTarget(mstate->vstate);
  if ((opcode & 0xf0) == 0x70 || opcode == 0xeb ||
      opcode == 0xe0 || opcode == 0xe1 || opcode == 0xe2 || opcode == 0xe3) {
    RememberTP(mstate->inst.vaddr, target, mstate->vstate);
  } else {
    /* If this ever happens, it's probably a decoder bug. */
    vprint(("ERROR: JMP8 %"PRIxPcAddress": %x\n", mstate->inst.vaddr, opcode));
    Stats_InternalError(mstate->vstate);
  }
}

static void ValidateJmpz(const struct NCDecoderState *mstate) {
  uint8_t *opcode = mstate->inst.maddr + mstate->inst.prefixbytes;
  int32_t offset;
  PcAddress target;
  Stats_CheckTarget(mstate->vstate);
  if (*opcode == 0xe8 || *opcode == 0xe9) {
    offset = *(int32_t *)&opcode[1];
    target = mstate->inst.vaddr + mstate->inst.length + offset;
    RememberTP(mstate->inst.vaddr, target, mstate->vstate);
    /* as a courtesy, check call alignment correctness */
    if (*opcode == 0xe8) ValidateCallAlignment(mstate);
  } else if (*opcode == 0x0f) {
    if ((opcode[1] & 0xf0) == 0x80) {
      offset = *(int32_t *)&opcode[2];
      target = mstate->inst.vaddr + mstate->inst.length + offset;
      RememberTP(mstate->inst.vaddr, target, mstate->vstate);
    }
  } else {
    /* If this ever happens, it's probably a decoder bug. */
    vprint(("ERROR: JMPZ %"PRIxPcAddress": %x %x\n",
             mstate->inst.vaddr, opcode[0], opcode[1]));
    Stats_InternalError(mstate->vstate);
  }
}

/*
 * The NaCl 14-byte safe indirect calling sequence looks like this:
 *   81 e0 ff ff ff ff        and  $0xffffffff,%eax
 *   81 c8 00 00 00 00        or   $0x0,%eax
 *   ff d0                    call *%eax
 * The call may be replaced with a ff e0 jmp. Any register may
 * be used, not just eax. The validator requires exactly this
 * sequence.
 * TODO(brad): validate or write the masks.
 */
#ifdef NACLJMP14
NOTE: This routine has not been kept up to date, and should be updated
  with respect to the five-byte version (below) if we need to use it
  again someday.
static void ValidateIndirect14(const struct NCDecoderState *mstate) {
  uint8_t               *jmpopcode;
  uint8_t               *oropcode;
  uint8_t               *andopcode;
  uint8_t               mrm;
  uint8_t               reg;

  struct NCDecoderState *andinst = PreviousInst(mstate, -2);
  struct NCDecoderState *orinst = PreviousInst(mstate, -1);
  assert(andinst != NULL);
  assert(orinst != NULL);
  if ((andinst->inst.length == 0) || (orinst->inst.length == 0)) {
    ValidatePrintError(mstate->inst.vaddr, "Unsafe indirect jump");
    Stats_UnsafeIndirect(mstate->vstate);
    return;
  }
  /* no prefix bytes allowed; checked below */
  jmpopcode = mstate->inst.maddr;
  oropcode = orinst->inst.maddr;
  andopcode = andinst->inst.maddr;
  mrm = jmpopcode[1];
  reg = modrm_rm(mrm);

  Stats_CheckTarget(mstate->vstate);
  Stats_TargetIndirect(mstate->vstate);
  do {
    /* check no prefix bytes */
    if (mstate->inst.prefixbytes != 0) break;
    if (andinst->inst.prefixbytes != 0) break;
    if (orinst->inst.prefixbytes != 0) break;
    /* check all the opcodes. */
    if (jmpopcode[0] != 0xff) break;
    if ((modrm_reg(mrm) != 2) && (modrm_reg(mrm) != 4)) break;
    /* Issue 32: disallow unsafe call/jump indirection */
    /* example:    ff 12     call (*edx)               */
    /* Reported by defend.the.world on 11 Dec 2008     */
    if (modrm_mod(mrm) != 3) break;
    if (oropcode[0] != 0x81) break;
    if (andopcode[0] != 0x81) break;
    /* check modrm bytes of or and and instructions */
    if (andopcode[1] != (0xe0 | reg)) break;
    if (oropcode[1] != (0xc8 | reg)) break;
    /* check mask */
    assert(0); /* not implemented yet, so don't use this routine! */
    /* All checks look good. Make the sequence 'atomic.' */
    ForgetIP(orinst->inst.vaddr, mstate->vstate);
    ForgetIP(mstate->inst.vaddr, mstate->vstate);
    return;
  } while (0);
  ValidatePrintError(mstate->inst.vaddr, "Unsafe indirect jump");
  Stats_UnsafeIndirect(mstate->vstate);
}
#endif

/*
 * The NaCl five-byte safe indirect calling sequence looks like this:
 *   83 e0 f0                 and  $0xfffffff0,%eax
 *   ff d0                    call *%eax
 * The call may be replaced with a ff e0 jmp. Any register may
 * be used, not just eax. The validator requires exactly this
 * sequence.
 * TODO(brad): validate or write the masks.
 */
static void ValidateIndirect5(const struct NCDecoderState *mstate) {
  uint8_t               *jmpopcode;
  uint8_t               *andopcode;
  uint8_t               mrm;
  uint8_t               targetreg;
  const uint8_t         kReg_ESP = 4;

  struct NCDecoderState *andinst = PreviousInst(mstate, -1);
  assert(andinst != NULL);
  if (andinst->inst.length == 0) {
    ValidatePrintError(mstate->inst.vaddr, "Unsafe indirect jump");
    Stats_UnsafeIndirect(mstate->vstate);
    return;
  }
  jmpopcode = mstate->inst.maddr;   /* note: no prefixbytes allowed */
  andopcode = andinst->inst.maddr;  /* note: no prefixbytes allowed */
  mrm = jmpopcode[1];
  targetreg = modrm_rm(mrm);  /* Note that the modrm_rm field holds the   */
                              /* target addr the modrm_reg is the opcode. */

  Stats_CheckTarget(mstate->vstate);
  Stats_TargetIndirect(mstate->vstate);
  do {
    /* no prefix bytes allowed */
    if (mstate->inst.prefixbytes != 0) break;
    if (andinst->inst.prefixbytes != 0) break;
    /* Check all the opcodes. */
    /* In GROUP5, 2 => call, 4 => jmp */
    if (jmpopcode[0] != 0xff) break;
    if ((modrm_reg(mrm) != 2) && (modrm_reg(mrm) != 4)) break;
    /* Issue 32: disallow unsafe call/jump indirection */
    /* example:    ff 12     call (*edx)               */
    /* Reported by defend.the.world on 11 Dec 2008     */
    if (modrm_mod(mrm) != 3) break;
    if (targetreg == kReg_ESP) break;
    if (andopcode[0] != 0x83) break;
    /* check modrm bytes of or and and instructions */
    if (andopcode[1] != (0xe0 | targetreg)) break;
    /* check mask */
    if (andopcode[2] != (0x0ff & ~mstate->vstate->alignmask)) break;
    /* All checks look good. Make the sequence 'atomic.' */
    ForgetIP(mstate->inst.vaddr, mstate->vstate);
    /* as a courtesy, check call alignment correctness */
    if (modrm_reg(mrm) == 2) ValidateCallAlignment(mstate);
    return;
  } while (0);
  ValidatePrintError(mstate->inst.vaddr, "Unsafe indirect jump");
  Stats_UnsafeIndirect(mstate->vstate);
}

/* It appears to me that none of my favorite test programs use more */
/* than a single prefix byte on an instruction. It would be nice if */
/* we could make this a requirement.                                */
static const size_t kMaxValidPrefixBytes = 1;
static const size_t kMaxValidInstLength = 11;
static const uint8_t kNaClFullStop = 0xf4;   /* x86 HALT opcode */

void ValidateInst(const struct NCDecoderState *mstate) {
  /* NaCl allows at most 1 prefix byte per instruction */
  const int kMaxValPrefixBytes = 1;
  CPUFeatures *cpufeatures;
  int squashme = 0;

  /*  dprint(("ValidateInst(%x, %x) at %x\n",
      (uint32_t)mstate, (uint32_t)mstate->vstate, mstate->inst.vaddr)); */
  if (mstate == NULL) return;
  if (mstate->vstate == NULL) return;
  OpcodeHisto(mstate->inst.maddr[mstate->inst.prefixbytes],
              mstate->vstate);
  RememberIP(mstate->inst.vaddr, mstate->vstate);
  cpufeatures = &(mstate->vstate->cpufeatures);
  do {
    if (mstate->inst.prefixbytes == 0) break;
    if (mstate->inst.prefixbytes <= kMaxValPrefixBytes) {
      if (mstate->inst.hasopbyte2) {
        if (mstate->inst.prefixmask & kPrefixLOCK) {
          /* For two byte opcodes, check for use of the lock prefix.   */
          if (mstate->opinfo->insttype == NACLi_386L) break;
          if (mstate->opinfo->insttype == NACLi_CMPXCHG8B) break;
        } else {
          /* Other prefixes checks are encoded in the two-byte tables. */
          break;
        }
      } else {
        /* one byte opcode */
        if ((mstate->inst.prefixmask &
             BadPrefixMask[mstate->opinfo->insttype]) == 0) break;
      }
    }
    ValidatePrintError(mstate->inst.vaddr, "Bad prefix usage");
    Stats_BadPrefix(mstate->vstate);
  } while (0);
  if ((size_t) (mstate->inst.length - mstate->inst.prefixbytes)
      > kMaxValidInstLength) {
    ValidatePrintError(mstate->inst.vaddr, "Instruction too long");
    Stats_BadInstLength(mstate->vstate);
  }
  switch (mstate->opinfo->insttype) {
    case NACLi_386:
    case NACLi_386L:
    case NACLi_386R:
    case NACLi_386RE:
      break;
    case NACLi_JMP8:
      ValidateJmp8(mstate);
      break;
    case NACLi_JMPZ:
      ValidateJmpz(mstate);
      break;
    case NACLi_INDIRECT:
      ValidateIndirect5(mstate);
      break;
    case NACLi_X87:
      squashme = (!cpufeatures->f_x87);
      break;
    case NACLi_SFENCE_CLFLUSH:
      squashme = ValidateSFenceClFlush(mstate);
      break;
    case NACLi_CMPXCHG8B:
      squashme = (!cpufeatures->f_CX8);
      break;
    case NACLi_CMOV:
      squashme = (!cpufeatures->f_CMOV);
      break;
    case NACLi_FCMOV:
      squashme = (!(cpufeatures->f_CMOV && cpufeatures->f_x87));
      break;
    case NACLi_RDTSC:
      squashme = (!cpufeatures->f_TSC);
      break;
    case NACLi_MMX:
      squashme = (!cpufeatures->f_MMX);
      break;
    case NACLi_MMXSSE2:
      /* Note: We accept these instructions if either MMX or SSE2 bits */
      /* are set, in case MMX instructions go away someday...          */
      squashme = (!(cpufeatures->f_MMX || cpufeatures->f_SSE2));
      break;
    case NACLi_SSE:
      squashme = (!cpufeatures->f_SSE);
      break;
    case NACLi_SSE2:
      squashme = (!cpufeatures->f_SSE2);
      break;
    case NACLi_SSE3:
      squashme = (!cpufeatures->f_SSE3);
      break;
    case NACLi_SSE4A:
      squashme = (!cpufeatures->f_SSE4A);
      break;
    case NACLi_SSE41:
      squashme = (!cpufeatures->f_SSE41);
      break;
    case NACLi_SSE42:
      squashme = (!cpufeatures->f_SSE42);
      break;
    case NACLi_MOVBE:
      squashme = (!cpufeatures->f_MOVBE);
      break;
    case NACLi_POPCNT:
      squashme = (!cpufeatures->f_POPCNT);
      break;
    case NACLi_LZCNT:
      squashme = (!cpufeatures->f_LZCNT);
      break;
    case NACLi_SSSE3:
      squashme = (!cpufeatures->f_SSSE3);
      break;
    case NACLi_3DNOW:
      squashme = (!cpufeatures->f_3DNOW);
      break;
    case NACLi_E3DNOW:
      squashme = (!cpufeatures->f_E3DNOW);
      break;
    case NACLi_SSE2x:
      /* This case requires CPUID checking code */
      /* DATA16 prefix required */
      if (!(mstate->inst.prefixmask & kPrefixDATA16)) {
        ValidatePrintError(mstate->inst.vaddr, "Bad prefix usage");
        Stats_BadPrefix(mstate->vstate);
      }
      squashme = (!cpufeatures->f_SSE2);
      break;

    case NACLi_RETURN:
      ValidatePrintError(mstate->inst.vaddr, "ret instruction (not allowed)");
      Stats_Return(mstate->vstate);
      /* ... and fall through to illegal instruction code */
    case NACLi_EMMX:
      /* EMMX needs to be supported someday but isn't ready yet. */
    case NACLi_INVALID:
    case NACLi_ILLEGAL:
    case NACLi_SYSTEM:
    case NACLi_RDMSR:
    case NACLi_RDTSCP:
    case NACLi_SYSCALL:
    case NACLi_SYSENTER:
    case NACLi_LONGMODE:
    case NACLi_SVM:
    case NACLi_OPINMRM:
    case NACLi_3BYTE:
    case NACLi_CMPXCHG16B: {
        /* uint8_t *opcode = mstate->inst.maddr + mstate->inst.prefixbytes; */
        ValidatePrintError(mstate->inst.vaddr, "Illegal instruction");
        Stats_IllegalInst(mstate->vstate);
        break;
      }
    case NACLi_UNDEFINED: {
        /* uint8_t *opcode = mstate->inst.maddr + mstate->inst.prefixbytes; */
        ValidatePrintError(mstate->inst.vaddr, "Undefined instruction");
        Stats_IllegalInst(mstate->vstate);
        Stats_InternalError(mstate->vstate);
        break;
      }
    default:
      ValidatePrintError(mstate->inst.vaddr, "Undefined instruction type");
      Stats_InternalError(mstate->vstate);
      break;
  }
  if (squashme) memset(mstate->inst.maddr, kNaClFullStop, mstate->inst.length);
}

void NCValidateSegment(uint8_t *mbase, uint32_t vbase, size_t sz,
                       struct NCValidatorState *vstate) {
  if (sz == 0) {
    ValidatePrintError(0, "Bad text segment (zero size)");
    Stats_MissingFullStop(vstate);
    Stats_SegFault(vstate);
    return;
  }
  GetCPUFeatures(&(vstate->cpufeatures));
  /* The name of the flag is misleading; f_386 requires not just    */
  /* 386 instructions but also the CPUID instruction is supported.  */
  if (!vstate->cpufeatures.f_386) {
    ValidatePrintError(0, "CPU does not support CPUID");
    Stats_BadCPU(vstate);
    return;
  }
#if (0)
  /* TODO(bradchen): enable this check */
  if (!vstate->cpufeatures.f_whitelisted) {
    ValidatePrintError(0, "CPU does not support CPUID");
    Stats_BadCPU(vstate);
    return;
  }
#endif

  g_print_diagnostics = 0; /* Supress confusing validator errors. */
  NCDecodeSegment(mbase, vbase, sz-1, vstate);
  if (mbase[sz-1] != kNaClFullStop) {
    Stats_MissingFullStop(vstate);
  }
  g_print_diagnostics = 1;
}
