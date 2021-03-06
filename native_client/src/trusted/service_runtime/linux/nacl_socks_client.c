#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <openssl/sha.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <errno.h>

#include "native_client/src/trusted/service_runtime/linux/nacl_socks_client.h"
#include "native_client/src/trusted/service_runtime/nacl_app_thread.h"
#include "native_client/src/trusted/service_runtime/sel_ldr.h"

#define NUM_REMOTE_SERVER_PORTS 20
struct NaClRemoteServerPorts remoteServerPortsRingbuffer[NUM_REMOTE_SERVER_PORTS];
int rsprb_index = 0;


int verifyServerPort(struct NaClRemoteServerPorts *p, uint16_t port) {
  return (((p->ports[port/8]) & (1 << (port % 8))) != 0);
}


int NaClIsConnectionOk(const struct sockaddr *addr, unsigned char* hash) {
  // We've verified that this must (well, is supposed to) be an inet struct
  struct sockaddr_in *addr_in = (struct sockaddr_in *)addr;

  int i, r;

  unsigned char buf[24];
  char ret_buf[8200];
  struct NaClRemoteServerPorts *remote_ports;

  struct sockaddr_in to;
  int sockfd;

  if (addr->sa_family != AF_INET) {
    return -1; // We don't support anything != IPv4 at this time
  }

  // See if we already know what to do with this IP
  for (i = 0; i < NUM_REMOTE_SERVER_PORTS; i++) {
    if (remoteServerPortsRingbuffer[i].ip == addr_in->sin_addr.s_addr) {
      return verifyServerPort(&remoteServerPortsRingbuffer[i], addr_in->sin_port);
    }
  }

  // We don't.  So, go fetch a response, and use it.
  MakeNaClHashReq(&buf[0], hash, 0);
  
  // Set up the remote server to send to
  to.sin_port = htons(NACL_VALIDATE_SERVERPORT);
  to.sin_family = AF_INET;
  to.sin_addr = addr_in->sin_addr;
  
  if ((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)  {
    return sockfd;
  }

  if ((r = connect(sockfd, &to, sizeof(to))) < 0) {
    return r;
  }
  
  if ((r = send(sockfd, &buf, sizeof(buf), 0)) < 0) {
    return -1;
  }

  // Zero ret_buf, so that on the off chance the server
  // is broken and sends us too little data,
  // we're conservative and assume not to use unspecified ports.
  memset(&ret_buf[0], 0, sizeof(ret_buf));
  if ((r = recv(sockfd, &ret_buf[0], sizeof(ret_buf), 0)) < 0) {
    return r;
  }
  
  close(sockfd);

  if ((r = ParseNaClHashResp(&ret_buf[0], sizeof(ret_buf), addr_in->sin_addr.s_addr, &remote_ports, 0)) != 0) {
    return r;
  }

  return verifyServerPort(remote_ports, addr_in->sin_port);
}


/*Given a hash & nonce, fill in the buf with the message that needs to be sent to server*/
/*The buffer must have a length of 24 bytes.  The response will be 24 bytes long.*/
void MakeNaClHashReq(unsigned char *buf, unsigned char *hash, uint32_t nonce) {
  memcpy(buf, hash, 20);
  memcpy(buf+20, &nonce, 4);
}

/*Given the response from the server, fills in and sets ports to point to the NaClRemoteServerPorts struct*/
/*returns 0 on success nonzero on error*/
int ParseNaClHashResp(const char* buf, uint32_t buf_len, uint32_t server_ip, struct NaClRemoteServerPorts **ports, uint32_t nonce) {
  /* Format is as follows:
   * 
   * First 32 bits: success code (4-byte integer, in network order) -- 0 indicates success
   * Next 32 bits: Nonce that we sent to the server initially
   * Next 65536 bits: Bitmask of allowed ports
   */
  uint32_t *int_ptr;
  uint32_t success;
  uint32_t new_nonce;
  struct NaClRemoteServerPorts *p;

  if (buf_len < 8200) {
    return -1; // We have to be able to parse out at least our three integers of interest
  }

  int_ptr = (uint32_t*)buf;
  success = ntohl(int_ptr[0]);
  new_nonce = ntohl(int_ptr[1]);

  if (success != 0) {
    return success;
  }

  if (new_nonce != nonce) {
    return -1;
  }

  p = &remoteServerPortsRingbuffer[rsprb_index++ % NUM_REMOTE_SERVER_PORTS];

  p->ip = server_ip;
  memcpy(&p->ports[0], buf + 8, 8192);

  *ports = p;

  return 0;
}

/*Given the filename of nexe, create a hash*/
/*"hash" must point to a 20-bytes buffer into which the hash will be written.*/
/*returns 0 on success nonzero on error*/
int MakeNaClHash(const char* nexe_file, unsigned char *hash) {
  int fd;
  struct stat filstat;
  int r;
  void *addr = 0;  

  if ((fd = open(nexe_file, O_RDONLY)) < 0) {
    return fd;
  }
  
  if ((r = fstat(fd, &filstat)) < 0) {
    return r;
  }

  addr = mmap(addr, filstat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

  SHA1((const unsigned char *)addr, filstat.st_size, hash);

  return 0;
}
