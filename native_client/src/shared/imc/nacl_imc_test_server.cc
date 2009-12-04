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


// NaCl inter-module communication primitives.

// TODO(shiki): Make this program a unit test.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "native_client/src/shared/imc/nacl_imc.h"

nacl::SocketAddress server_address = {
  "imc-server"
};

nacl::SocketAddress client_address = {
  "imc-client"
};

namespace {

nacl::Handle g_front;

void CleanUp() {
  nacl::Close(g_front);
}

// Writes the last error message to the standard error.
void PrintError(const char* message) {
  char buffer[256];

  if (nacl::GetLastErrorString(buffer, sizeof buffer) == 0) {
    fprintf(stderr, "%s: %s\n", message, buffer);
  }
}

}  // namespace

int main() {
  int result;

  g_front = nacl::BoundSocket(&server_address);
  if (g_front == nacl::kInvalidHandle) {
    PrintError("BoundSocket");
    exit(EXIT_FAILURE);
  }
  atexit(CleanUp);

  // Test name collision.
  nacl::Handle front2 = nacl::BoundSocket(&server_address);
  assert(front2 == nacl::kInvalidHandle);
  if (front2 == nacl::kInvalidHandle) {
    PrintError("BoundSocket");
  }

  nacl::Handle handles[8];
  nacl::MessageHeader header;
  nacl::IOVec vec[3];
  char buffer[64];

  // Receive a handle connected to the client.
  nacl::Handle client_handle = nacl::kInvalidHandle;
  vec[0].base = buffer;
  vec[0].length = sizeof buffer;
  header.iov = vec;
  header.iov_length = 1;
  header.handles = handles;
  header.handle_count = sizeof handles / sizeof handles[0];
  result = nacl::ReceiveDatagram(g_front, &header, 0);
  if (0 < result && header.handle_count == 1) {
    client_handle = handles[0];
    printf("%.*s: %d\n", result, buffer, client_handle);
  } else {
    PrintError("ReceiveDatagram");
    exit(EXIT_FAILURE);
  }

  // Test kDontWait for a socket created by socketpair().
  vec[0].base = buffer;
  vec[0].length = sizeof buffer;
  header.iov = vec;
  header.iov_length = 1;
  header.handles = NULL;
  header.handle_count = 0;
  result = nacl::ReceiveDatagram(client_handle, &header, nacl::kDontWait);
  assert(result == -1);
  PrintError("ReceiveDatagram");
  assert(nacl::WouldBlock());

  // Test an empty message.
  vec[0].base = buffer;
  vec[0].length = 0;
  header.iov = vec;
  header.iov_length = 1;
  header.handles = NULL;
  header.handle_count = 0;
  result = nacl::SendDatagram(client_handle, &header, 0);
  assert(result == 0);

  // Test scatter/gather.
  for (unsigned int i = 0; i < sizeof vec / sizeof vec[0]; ++i) {
    buffer[i] = "ABC"[i];
    vec[i].base = buffer + i;
    vec[i].length = 1;
  }
  header.iov = vec;
  header.iov_length = sizeof vec / sizeof vec[0];
  header.handles = &client_handle;
  header.handle_count = 1;
  result = nacl::SendDatagram(client_handle, &header, 0);
  assert(result == sizeof vec / sizeof vec[0]);

  // Test Receive().
  result = nacl::Receive(client_handle, buffer, sizeof buffer, 0);
  if (result == -1) {
    PrintError("ReceiveDatagram");
  }
  assert(result == 2);
  if (0 < result) {
    printf("%.*s\n", result, buffer);
  }

  // Test shared memory.
  nacl::Handle shared_memory = nacl::CreateMemoryObject(1024 * 1024);
  if (shared_memory != nacl::kInvalidHandle) {
    void* shared_region = nacl::Map(0, 1024 * 1024,
                                    nacl::kProtRead | nacl::kProtWrite,
                                    nacl::kMapShared,
                                    shared_memory, 0);
    if (shared_region) {
      memset(shared_region, 0, 1024 * 1024);

      header.iov = vec;
      header.iov_length = 2;
      header.handles = &shared_memory;
      header.handle_count = 1;
      nacl::SendDatagram(client_handle, &header, 0);

      memset(shared_region, 0xff, 1024 * 1024);
      while (*static_cast<volatile unsigned int*>(shared_region) ==
             0xffffffff) {
      }
      printf("server: shm ok.\n");
    }

    nacl::Unmap(shared_region, 1024 * 1024);
    nacl::Close(shared_memory);
  }

  // Test kDontWait for a large message.
  vec[0].base = buffer;
  vec[0].length = sizeof buffer;
  header.iov = vec;
  header.iov_length = 1;
  header.handles = NULL;
  header.handle_count = 0;
  result = nacl::ReceiveDatagram(client_handle, &header, nacl::kDontWait);
  assert(result == sizeof buffer);

  // Test close detection.
  vec[0].base = buffer;
  vec[0].length = sizeof buffer;
  header.iov = vec;
  header.iov_length = 1;
  header.handles = NULL;
  header.handle_count = 0;
  result = nacl::ReceiveDatagram(client_handle, &header, 0);
  assert(result <= 0);

  nacl::Close(client_handle);
  return 0;
}