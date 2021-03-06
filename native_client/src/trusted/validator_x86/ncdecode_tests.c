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
 * ncdecode_tests.c - simple unit tests for NaCl decoder
 */
#include <stdio.h>
#include "native_client/src/trusted/validator_x86/ncdecode.h"

struct NCDecodeTestCase {
  char *description;
  size_t testsize;
  uint8_t *testbytes;
};

struct NCDecodeTestCase NCDecoderTests[] = {
  {
    "test 1",
    25,
    (uint8_t *)"\x55"
    "\x89\xe5"
    "\x83\xec\x08"
    "\xe8\x81\x00\x00\x00"
    "\xe8\xd3\x00\x00\x00"
    "\xe8\xf3\x04\x00\x00"
    "\xc9"
    "\xc3"
    "\x00\x00",
  },
  {
    "test2",
    342,
    (uint8_t *)"\x8d\x4c\x24\x04"
    "\x83\xe4\xf0"
    "\xff\x71\xfc"
    "\x55"
    "\x89\xe5"
    "\x51"
    "\x66\x90"
    "\x83\xec\x24"
    "\x89\x4d\xe8"
    "\xc7\x45\xf4\x0a\x00\x00\x00"
    "\x8b\x45\xe8"
    "\x83\x38\x01"
    "\x7f\x2b"
    "\x8b\x55\xe8"
    "\x8b\x42\x04"
    "\x8b\x00"
    "\x8d\x76\x00"
    "\x89\x44\x24\x04"
    "\xc7\x04\x24\x54\x14\x00\x08"
    "\xe8\xc0\x02\x00\x00"
    "\xc7\x04\x24\x01\x00\x00\x00"
    "\x8d\x74\x26\x00"
    "\xe8\xc0\x01\x00\x00"
    "\x8b\x55\xe8"
    "\x8b\x42\x04"
    "\x83\xc0\x04"
    "\x8b\x00"
    "\x89\x04\x24"
    "\x66\x90"
    "\x8d\x74\x26\x00"
    "\x8d\xbc\x27\x00\x00\x00\x00"
    "\xe8\x90\x09\x00\x00"
    "\x89\x45\xf8"
    "\x8b\x45\xe8"
    "\x83\x38\x02"
    "\x7e\x25"
    "\x8b\x55\xe8"
    "\x66\x90"
    "\x8b\x42\x04"
    "\x83\xc0\x08"
    "\x8b\x00"
    "\x89\x04\x24"
    "\xe8\x70\x09\x00\x00"
    "\x89\x45\xf4"
    "\x8d\xb6\x00\x00\x00\x00"
    "\x8d\xbc\x27\x00\x00\x00\x00"
    "\x8b\x45\xf4"
    "\xa3\x28\x2f\x00\x08"
    "\xeb\x26"
    "\x8d\xb6\x00\x00\x00\x00"
    "\xc7\x44\x24\x08\x03\x00\x00\x00, "
    "\xc7\x44\x24\x04\x01\x00\x00\x00, "
    "\x8b\x45\xf4"
    "\x89\x04\x24"
    "\x90"
    "\x8d\x74\x26\x00"
    "\xe8\x20\x00\x00\x00"
    "\x83\x7d\xf8\x00"
    "\x0f\x9f\xc0"
    "\x83\x6d\xf8\x01"
    "\x84\xc0"
    "\x8d\x76\x00"
    "\x75\xce"
    "\xc7\x04\x24\x00\x00\x00\x00"
    "\x66\x90"
    "\xe8\x20\x01\x00\x00"
    "\x55"
    "\x89\xe5"
    "\x83\xec\x1c"
    "\x83\x7d\x08\x01"
    "\x75\x44"
    "\x8b\x55\x0c"
    "\x90"
    "\x8b\x04\x95\x24\x2f\x00\x08"
    "\x83\xe8\x01"
    "\x8d\xb6\x00\x00\x00\x00"
    "\x89\x04\x95\x24\x2f\x00\x08"
    "\x8b\x55\x10"
    "\x8d\xb6\x00\x00\x00\x00"
    "\x8b\x04\x95\x24\x2f\x00\x08"
    "\x83\xc0\x01"
    "\x8d\xb6\x00\x00\x00\x00"
    "\x89\x04\x95\x24\x2f\x00\x08"
    "\xeb\x77"
    "\x8d\xb4\x26\x00\x00\x00\x00"
    "\x8b\x45\x10"
    "\x8b\x55\x0c"
    "\x01\xc2"
    "\xb8\x06\x00\x00\x00"
    "\x29\xd0"
    "\x90"
  },
  {
    "test 3",
    9,
    (uint8_t *)"\x3c\x25"
    "\x90\x90\x90\x90\x90\x90\x90"
  },
  { "test4",
    13,
    (uint8_t *)"\xc1\xf9\x1f\x89\x4d\xe4"
    "\x90\x90\x90\x90\x90\x90\x90"
  },
  {
    "test5",
    12,
    (uint8_t *)"\xc6\x44\x05\xd6\x00"
    "\x90\x90\x90\x90\x90\x90\x90"
  },
  {
    "test6",
    14,
    (uint8_t *)"\xff\x24\x95\xc8\x6e\x05\x08"
    "\x90\x90\x90\x90\x90\x90\x90"
  },
  {
    "test7",
    15,
    (uint8_t *)"\x0f\xab\x14\x85\x40\xfb\x27\x08"
    "\x90\x90\x90\x90\x90\x90\x90"
  },
  {
    "test8",
    11,
    (uint8_t *)"\x66\xbf\x08\x00"
    "\x90\x90\x90\x90\x90\x90\x90"
  },
  {
    "test9",
    12,
    (uint8_t *)"\x66\x0f\xbe\x04\x10"
    "\x90\x90\x90\x90\x90\x90\x90"
  },
  /* ldmxcsr, stmxcsr */
  {
    "test10",
    15,
    (uint8_t *)"\x90\x0f\xae\x10\x90\x0f\xae\x18"
    "\x90\x90\x90\x90\x90\x90\x90"
  },
  /* invalid */
  {
    "test11",
    11,
    (uint8_t *)"\x90\x0f\xae\x21"
    "\x90\x90\x90\x90\x90\x90\x90"
  },
  /* lfence */
  {
    "test12",
    11,
    (uint8_t *)"\x90\x0f\xae\x28"
    "\x90\x90\x90\x90\x90\x90\x90"
  },
  {
    "test13",
    9,
    (uint8_t *)"\xf0\x0f\xb1\x8f\xa8\x01\x00\x00"
    "\x90"
  },
  {
    "test14",
    17,
    (uint8_t *)"\x84\xd4\x04\x53\xa0\x04\x6a\x5a\x20\xcc\xb8\x48\x03"
    "\x2b\x96\x11\xf4"
  },
  {
    "test15",
    17,
    (uint8_t *)"\xb0\xb9\xd8\x3d\x60\xe4\x2c\x15\x43\x61\x4e\x03\x39"
    "\xfc\x79\x4b\xf4"
  },
  {
    "test16",
    17,
    (uint8_t *)"\x66\x66\x66\x66\x0f\x1f\x0d\x1a\xed\x25\x37\x27\x0a"
    "\xff\xc6\x1a\xf4"
  },
  {
    "test17",
    18,
    (uint8_t *)"\x66\xeb\x1b\x31\x51\x3d\xef\xcc\x2f\x36\x48\x6e\x44"
    "\x2e\xcc\x14\x00\xf4"
  },
  {
    "test18",
    17,
    (uint8_t *)"\x67\x8d\x1d\x22\xa0\x05\xe3\x7b\x9c\xdb\x08\x04\xb1"
    "\x90\xed\x12\xf4"
  },
  {
    "test 19",
    20,
    (uint8_t *)"\x45\x7f\x89\x58\x94\x04\x24\x1b\xc3\xe2\x6f\x1a\x94\x87\x8f\x0b\x00\x00\x90\xf4",
  },
  {
    "test 20",
    8,
    (uint8_t *)"\x66\x0f\x3a\x0e\xd0\xc0\x90\xf4"
  },
  {
    "test 21: palignr (SSSE3)",
    8,
    (uint8_t *)"\x66\x0f\x3a\x0f\xd0\xc0\x90\xf4"
  },
  {
    "test 22: SSSE3",
    8,
    (uint8_t *)"\x66\x0f\x38\x00\x00\x00\x90\xf4"
  },
  {
    "test 23: 3DNow",
    7,
    (uint8_t *)"\x0f\x0f\x46\x01\xbf\x90\xf4"
  },
  {
    "test 24: SSSE3 error",
    8,
    (uint8_t *)"\x66\x0f\x3a\x0e\xd0\xc0\x90\xf4"
  },
  {
    "test 25: lzcnt",
    6,
    (uint8_t *)"\xf3\x0f\xbd\x00\x90\xf4",
  },
  {
    "test 26: 3dnow",
    6,
    (uint8_t *)"\x0f\x0f\x01\x0c\x90\xf4",
  },
  {
    "test 27: addr16 variants",
    17,
    (uint8_t *)"\x67\x01\x00"
    "\x67\x01\x40\x00"
    "\x67\x01\x80\x00\x90"
    "\x67\x01\xc0"
    "\x90\xf4",
  },
  {
    "test 28: lock cmpxchg8b",
    6,
    (uint8_t *)"\xf0\x0f\xc7\010\x90\xf4"
  },
  {
    "test 29: SSE3",
    8,
    (uint8_t *)"\x66\x0f\x7d\x00\x90\x90\x90\xf4"
  },
  {
    "test 30: MMXSSE2",
    10,
    (uint8_t *)"\x66\x0f\x71\xf6\x00\x90\x90\x90\x90\xf4",
  },
  {
    "test 31: call %esp",
    7,
    /* 11 100 000 */
    (uint8_t *)"\x83\xe4\xf0\xff\xd4\x90\xf4",
  },
  {
    "test 32: roundss",
    9,
    (uint8_t *)"\x66\x0f\x3a\x0a\xc0\x00"
    "\x90\x90"
    "\xf4",
  },
  {
    "test 33: crc32",
    8,
    (uint8_t *)"\xf2\x0f\x38\xf1\xc3"
    "\x90\x90"
    "\xf4",
  },
  {
    "test 34: SSE4",
    8,
    (uint8_t *)"\x66\x0f\x38\x0a\xd0\x90\x90\xf4"
  }
};

#define sizeofA(array)  (sizeof(array)/sizeof(array[0]))
void ncdecode_unittests() {
  size_t i;

  for (i = 0; i < sizeofA(NCDecoderTests); i++) {
    printf("==========================================================\n");
    printf("%s\n", NCDecoderTests[i].description);
    NCDecodeSegment(NCDecoderTests[i].testbytes,
                    (uint32_t)(NCDecoderTests[i].testbytes),
                    NCDecoderTests[i].testsize,
                    NULL);
  }
}

/* TODO: move this into header */
extern void PrintInstStdout(const struct NCDecoderState *mstate);


int main() {
  NCDecodeRegisterCallbacks(PrintInstStdout, NULL, NULL, NULL);
  ncdecode_unittests();
  printf("PASSED\n");
  return 0;
}
