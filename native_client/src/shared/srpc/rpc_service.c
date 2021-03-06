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
 * Service descriptions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "native_client/src/shared/srpc/nacl_srpc.h"
#include "native_client/src/shared/srpc/nacl_srpc_internal.h"

#ifndef SIZE_T_MAX
# define SIZE_T_MAX (~((size_t) 0))
#endif


/*
 * The struct used to describe methods within services.
 */
struct NaClSrpcMethodDesc {
  char const  *name;
  char const  *input_types;
  char const  *output_types;
  /**
   * function pointer used to process calls to the named method
   */
  NaClSrpcMethod handler;
};
typedef struct NaClSrpcMethodDesc NaClSrpcMethodDesc;

/*
 * Forward declarations for static "built-in" methods.
 */
static NaClSrpcError ServiceDiscovery(NaClSrpcChannel* channel,
                                      NaClSrpcArg** in_args,
                                      NaClSrpcArg** out_args);
static NaClSrpcError GetTimes(NaClSrpcChannel* channel,
                              NaClSrpcArg** in_args,
                              NaClSrpcArg** out_args);
static NaClSrpcError SetTimingEnabled(NaClSrpcChannel* channel,
                                      NaClSrpcArg** in_args,
                                      NaClSrpcArg** out_args);

/*
 * Get the next text element (name, input types, or output types).  These
 * elements are delimited by ':', '\n', or '\0'.
 */
static char* GetOneElement(const char* string) {
  size_t len = 0;
  char* buf;

  while (':' != string[len] && '\n' != string[len] && '\0' != string[len]) {
    len++;
  }
  buf = malloc(len + 1);
  if (NULL == buf) {
    return NULL;
  }
  strncpy(buf, string, len);
  buf[len] = '\0';
  return buf;
}

/* * Get the components of one method description.  Returns a pointer to
 * the delimiter if successful or NULL if not.
 */
static const char* ParseOneEntry(const char* entry_fmt,
                                 char** name,
                                 char** input_types,
                                 char** output_types) {
  char* namebuf = NULL;
  char* insbuf = NULL;
  char* outsbuf = NULL;

  *name = NULL;
  *input_types = NULL;
  *output_types = NULL;

  /* Don't try to parse NULL strings. */
  if (NULL == entry_fmt) {
    goto cleanup;
  }
  /* Get the name into a buffer, and ensure it is followed by ':'. */
  namebuf = GetOneElement(entry_fmt);
  if (NULL == namebuf) {
    goto cleanup;
  }
  entry_fmt += strlen(namebuf);
  if (':' != *entry_fmt) {
    goto cleanup;
  }
  entry_fmt++;
  /* Get the input types into a buffer, and ensure it is followed by ':'. */
  insbuf = GetOneElement(entry_fmt);
  if (NULL == insbuf) {
    goto cleanup;
  }
  entry_fmt += strlen(insbuf);
  if (':' != *entry_fmt) {
    goto cleanup;
  }
  entry_fmt++;
  /* Get the output types into a buffer. */
  outsbuf = GetOneElement(entry_fmt);
  if (NULL == outsbuf) {
    goto cleanup;
  }
  entry_fmt += strlen(outsbuf);
  /* Copy the result strings out. */
  *name = namebuf;
  *input_types = insbuf;
  *output_types = outsbuf;
  /* Return a pointer to the next character after the elements. */
  return entry_fmt;

 cleanup:
  free(insbuf);
  free(namebuf);
  return NULL;
}

/*
 * The method tables passed to construction do not contain "intrinsic" methods
 * such as service discovery and shutdown.  Build a complete table including
 * those from a given input.
 */
static NaClSrpcMethodDesc* BuildMethods(
    const struct NaClSrpcHandlerDesc* methods,
    uint32_t* method_count) {
  static const char* kSDDescString = "service_discovery::C";
  NaClSrpcMethodDesc* complete_methods;
  uint32_t i;

  /* Compute the number of methods to export. */
  *method_count = 0;
  while (NULL != methods[*method_count].entry_fmt)
    ++*method_count;
  /* Add one extra method for service discovery. */
  ++*method_count;
  /* Allocate the method descriptors. One extra for NULL termination. */
  complete_methods = (NaClSrpcMethodDesc*)
      malloc((*method_count + 1) * sizeof(*complete_methods));
  if (NULL == complete_methods) {
    return NULL;
  }
  /* Copy the methods passed in, adding service discovery as element zero. */
  ParseOneEntry(kSDDescString,
                (char**) &complete_methods[0].name,
                (char**) &complete_methods[0].input_types,
                (char**) &complete_methods[0].output_types);
  complete_methods[0].handler = ServiceDiscovery;
  for (i = 0; i < *method_count - 1; ++i) {
    ParseOneEntry(methods[i].entry_fmt,
                  (char**) &complete_methods[i + 1].name,
                  (char**) &complete_methods[i + 1].input_types,
                  (char**) &complete_methods[i + 1].output_types);
    complete_methods[i + 1].handler = methods[i].handler;
  }
  /* Add the NULL terminator */
  complete_methods[*method_count].name = NULL;
  complete_methods[*method_count].input_types = NULL;
  complete_methods[*method_count].output_types = NULL;
  complete_methods[*method_count].handler = NULL;
  /* Return the array */
  return complete_methods;
}

/*
 * Builds a service discovery string from an array of method descriptions.
 */
static char* BuildSDString(const NaClSrpcMethodDesc* methods,
                           uint32_t method_count,
                           size_t *length) {
  uint32_t i;
  char* p;
  char* str;

  /* Find the total length of the method description strings.  */
  *length = 1;
  for (i = 0; i < method_count; ++i) {
    *length += strlen(methods[i].name) + 1 +
               strlen(methods[i].input_types) + 1 +
               strlen(methods[i].output_types) + 1;
  }
  /* Allocate the string. */
  str = (char*) malloc(*length + 1);
  if (NULL == str) {
    return NULL;
  }
  /* Concatenate the method description strings into the string. */
  p = str;
  for (i = 0; i < method_count; ++i) {
    size_t buf_limit = str + *length - p;
    /* TODO(robertm): explore use portability_io.h */
#if NACL_WINDOWS
    p += _snprintf(p, buf_limit, "%s:%s:%s\n",
                   methods[i].name,
                   methods[i].input_types,
                   methods[i].output_types);
#else
    p += snprintf(p, buf_limit, "%s:%s:%s\n",
                  methods[i].name,
                  methods[i].input_types,
                  methods[i].output_types);
#endif
  }
  *p = '\0';
  /* Return the resulting string. */
  return str;
}

/*
 * Create a service descriptor from an array of methods.
 */
int NaClSrpcServiceHandlerCtor(NaClSrpcService* service,
                               const NaClSrpcHandlerDesc* handler_desc) {
  char* service_str;
  size_t str_length;
  NaClSrpcMethodDesc* methods;
  uint32_t method_count;

  /* Add the service_discovery method to the table. */
  methods = BuildMethods(handler_desc, &method_count);
  if (NULL == methods) {
    return 0;
  }
  service_str = BuildSDString(methods, method_count, &str_length);
  if (NULL == service_str) {
    /* BUG: leaks methods */
    return 0;
  }
  service->service_string = service_str;
  service->service_string_length = str_length;
  service->rpc_descr = methods;
  service->rpc_count = method_count;
  return 1;
}

void FreeMethods(NaClSrpcMethodDesc* methods, uint32_t rpc_count) {
  uint32_t i;

  if (NULL == methods) {
    return;
  }
  for (i = 0; i < rpc_count; ++i) {
    if (NULL == methods[i].name) {
      /* We have reached the end of the portion set by ParseOneEntry calls. */
      break;
    }
    free((char*) methods[i].name);
    free((char*) methods[i].input_types);
    free((char*) methods[i].output_types);
  }
  free(methods);
}

int NaClSrpcServiceStringCtor(NaClSrpcService* service, const char* str) {
  NaClSrpcMethodDesc* methods = NULL;
  const char* p;
  uint32_t i;
  uint32_t rpc_count;
  size_t rpc_count_size_t;

  /* Count the number of rpc methods */
  rpc_count = 0;
  for (p = str; *p != '\0'; ) {
    char* next_p = strchr(p, '\n');
    if (NULL == next_p) {
      /* malformed input -- no remaining \n character was found */
      goto cleanup;
    }
    p = next_p + 1;
    ++rpc_count;
    if (0 == rpc_count) {
      /* uint32_t overflow detected. */
      goto cleanup;
    }
  }
  /*
   * The front end knows the next comparison is useless due to the range of
   * uint32_t.  And furthermore at least one version of gcc knows that a
   * cast doesn't really change that fact.  Hence, assign to a new variable
   * of size_t type.
   */
  rpc_count_size_t = (size_t) rpc_count;
  /* Allocate and clear the descriptor array */
  if (rpc_count_size_t >= SIZE_T_MAX / sizeof(*methods)) {
    goto cleanup;
  }
  methods = (NaClSrpcMethodDesc*) malloc(rpc_count_size_t * sizeof(*methods));
  if (NULL == methods) {
    goto cleanup;
  }
  memset(methods, 0, rpc_count_size_t * sizeof(*methods));
  /* Parse the list of method descriptions */
  p = str;
  for (i = 0; i < rpc_count; ++i) {
    const char* newline_loc;

    newline_loc = ParseOneEntry(p,
                                (char**) &methods[i].name,
                                (char**) &methods[i].input_types,
                                (char**) &methods[i].output_types);
    if (NULL == newline_loc || '\n' != *newline_loc) {
      goto cleanup;
    }
    p = newline_loc + 1;
    /* The handler is not set from service_discovery strings */
    methods[i].handler = NULL;
  }
  service->service_string = strdup(str);
  service->service_string_length = strlen(str);
  service->rpc_descr = methods;
  service->rpc_count = rpc_count;
  return 1;

 cleanup:
  FreeMethods(methods, rpc_count);
  return 0;
}

/*
 * Destroy a service descriptor.
 */
void NaClSrpcServiceDtor(NaClSrpcService* service) {
  if (NULL == service) {
    return;
  }
  /* Free the method descriptors. */
  FreeMethods((NaClSrpcMethodDesc*) service->rpc_descr, service->rpc_count);
  /* Free the service discovery string. */
  free((char*) service->service_string);
}

void NaClSrpcServicePrint(const NaClSrpcService *service) {
  uint32_t i;

  if (NULL == service) {
    printf("NULL service\n");
    return;
  }
  printf("RPC %-20s %-10s %-10s\n", "Name", "Input args", "Output args");
  for (i = 0; i < service->rpc_count; ++i) {
    printf("%3u %-20s %-10s %-10s\n",
           (unsigned int) i,
           service->rpc_descr[i].name,
           service->rpc_descr[i].input_types,
           service->rpc_descr[i].output_types);
  }
}

uint32_t NaClSrpcServiceMethodCount(const NaClSrpcService *service) {
  if (NULL == service) {
    return 0;
  }
  return service->rpc_count;
}

uint32_t NaClSrpcServiceMethodIndex(const NaClSrpcService* service,
                                    char const* name) {
  uint32_t i;

  if (NULL == service) {
    return kNaClSrpcInvalidMethodIndex;
  }
  for (i = 0; i < service->rpc_count;  ++i) {
    if (!strcmp(name, service->rpc_descr[i].name)) {
      return i;
    }
  }
  return kNaClSrpcInvalidMethodIndex;
}

int NaClSrpcServiceMethodNameAndTypes(const NaClSrpcService* service,
                                      uint32_t rpc_number,
                                      const char** name,
                                      const char** input_types,
                                      const char** output_types) {
  if (NACL_SRPC_GET_TIMES_METHOD == rpc_number) {
    *name = "NACL_SRPC_GET_TIMES_METHOD";
    *input_types = "";
    *output_types = "dddd";
  } else if (NACL_SRPC_TOGGLE_CHANNEL_TIMING_METHOD == rpc_number) {
    *name = "NACL_SRPC_TOGGLE_CHANNEL_TIMING_METHOD";
    *input_types = "";
    *output_types = "i";
  } else if (rpc_number >= service->rpc_count) {
    /* This ensures that the method is in the user-defined set. */
    return 0;
  } else {
    *name = service->rpc_descr[rpc_number].name;
    *input_types = service->rpc_descr[rpc_number].input_types;
    *output_types = service->rpc_descr[rpc_number].output_types;
  }
  return 1;
}

NaClSrpcMethod NaClSrpcServiceMethod(const NaClSrpcService* service,
                                     uint32_t rpc_number) {
  if (NULL == service) {
    return NULL;
  } else if (NACL_SRPC_GET_TIMES_METHOD == rpc_number) {
    return GetTimes;
  } else if (NACL_SRPC_TOGGLE_CHANNEL_TIMING_METHOD == rpc_number) {
    return SetTimingEnabled;
  } else if (rpc_number >= service->rpc_count) {
    return NULL;
  } else {
    return service->rpc_descr[rpc_number].handler;
  }
}

/*
 * "Built-in" methods.
 */

/*
 * The service_discovery method.
 */
static NaClSrpcError ServiceDiscovery(NaClSrpcChannel* channel,
                                      NaClSrpcArg** in_args,
                                      NaClSrpcArg** out_args) {
  if (NULL == channel->server) {
    return NACL_SRPC_RESULT_APP_ERROR;
  }
  if (out_args[0]->u.caval.count >= channel->server->service_string_length) {
    strncpy(out_args[0]->u.caval.carr,
            channel->server->service_string,
            channel->server->service_string_length);
    /* Set the length of the string actually returned. */
    out_args[0]->u.caval.count = channel->server->service_string_length;
    return NACL_SRPC_RESULT_OK;
  } else {
    return NACL_SRPC_RESULT_APP_ERROR;
  }
}

/*
 * Timing the SRPC infrastructure.
 */
static NaClSrpcError GetTimes(NaClSrpcChannel* channel,
                              NaClSrpcArg** in_args,
                              NaClSrpcArg** out_args) {
  NaClSrpcGetTimes(channel,
                   &out_args[0]->u.dval,
                   &out_args[1]->u.dval,
                   &out_args[2]->u.dval,
                   &out_args[3]->u.dval);
  return NACL_SRPC_RESULT_OK;
}

static NaClSrpcError SetTimingEnabled(NaClSrpcChannel* channel,
                                      NaClSrpcArg** in_args,
                                      NaClSrpcArg** out_args) {
  NaClSrpcToggleChannelTiming(channel, in_args[0]->u.ival);
  return NACL_SRPC_RESULT_OK;
}
