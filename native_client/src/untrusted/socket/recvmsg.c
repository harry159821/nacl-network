 //@author nizam
#include <errno.h>
#include <sys/types.h>
#include "native_client/src/trusted/service_runtime/include/sys/socket.h"
#include "native_client/src/untrusted/nacl/syscall_bindings_trampoline.h"

ssize_t recvmsg (int fd, struct msghdr *message, int flags) {
  int retval = NACL_SYSCALL(recvmsg)(fd, message, flags);
  if (retval < 0) {
	errno = -retval;
	return -1;
  }
  return retval;
}
