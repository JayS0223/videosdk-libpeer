#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <netdb.h>

#include "utils.h"
#include "socket.h"


int udp_socket_add_multicast_group(UdpSocket *udp_socket, Address *mcast_addr) {

  int ret = 0;
  struct ip_mreq imreq = {0};
  struct in_addr iaddr = {0};

  imreq.imr_interface.s_addr = INADDR_ANY;
  // IPV4 only
  imreq.imr_multiaddr.s_addr = mcast_addr->sin.sin_addr.s_addr;

  if ((ret = setsockopt(udp_socket->fd, IPPROTO_IP, IP_MULTICAST_IF, &iaddr, sizeof(struct in_addr))) < 0) {
    LOGE("Failed to set IP_MULTICAST_IF: %d", ret);
    return ret;
  }

  if ((ret = setsockopt(udp_socket->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &imreq, sizeof(struct ip_mreq))) < 0) {
    LOGE("Failed to set IP_ADD_MEMBERSHIP: %d", ret);
    return ret;
  }

  return 0;
}

int udp_socket_open(UdpSocket *udp_socket, int family, int port) {

  int ret;
  int reuse = 1;
  struct sockaddr *sa;
  socklen_t sock_len;

  udp_socket->bind_addr.family = family;
  switch (family) {
    case AF_INET6:
      udp_socket->fd = socket(AF_INET6, SOCK_DGRAM, 0);
      udp_socket->bind_addr.sin6.sin6_family = AF_INET6;
      udp_socket->bind_addr.sin6.sin6_port = htons(port);
      udp_socket->bind_addr.sin6.sin6_addr = in6addr_any;
      udp_socket->bind_addr.port = ntohs(udp_socket->bind_addr.sin6.sin6_port);
      sa = (struct sockaddr *)&udp_socket->bind_addr.sin6;
      sock_len = sizeof(struct sockaddr_in6);
      break;
    case AF_INET:
    default:
      udp_socket->fd = socket(AF_INET, SOCK_DGRAM, 0);
      udp_socket->bind_addr.sin.sin_family = AF_INET;
      udp_socket->bind_addr.sin.sin_port = htons(port);
      udp_socket->bind_addr.sin.sin_addr.s_addr = htonl(INADDR_ANY);
      sa = (struct sockaddr *)&udp_socket->bind_addr.sin;
      sock_len = sizeof(struct sockaddr_in);
      break;
  }

  if (udp_socket->fd < 0) {
    LOGE("Failed to create socket");
    return -1;
  }

  do {
    if ((ret = setsockopt(udp_socket->fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))) < 0) {
      LOGE("reuse failed");
      break;
    }

    if ((ret = bind(udp_socket->fd, sa, sock_len)) < 0) {
      LOGE("Failed to bind socket: %d", ret);
      break;
    }

    if (getsockname(udp_socket->fd, sa, &sock_len) < 0) {
      LOGE("Get socket info failed");
      break;
    }
  } while (0);

  if (ret < 0) {
    udp_socket_close(udp_socket);
    return -1;
  }

  switch (udp_socket->bind_addr.family) {
    case AF_INET6:
      udp_socket->bind_addr.port = ntohs(udp_socket->bind_addr.sin6.sin6_port);
      break;
    case AF_INET:
    default:
      udp_socket->bind_addr.port = ntohs(udp_socket->bind_addr.sin.sin_port);
      break;
  }   

  return 0;
}

void udp_socket_close(UdpSocket *udp_socket) {

  if (udp_socket->fd > 0) {
    close(udp_socket->fd);
  }
}

int udp_socket_sendto(UdpSocket *udp_socket, Address *addr, const uint8_t *buf, int len) {

  struct sockaddr *sa;
  socklen_t sock_len;
  int ret = -1;

  if (udp_socket->fd < 0) {
    LOGE("sendto before socket init");
    return -1;
  }

  // LOGI( "JAY Sending to %x:%d", addr_to_string(addr, NULL, 0), addr->port);
  char addr_str[64];
  addr_to_string(addr, addr_str, sizeof(addr_str));
  LOGI("JAY Sending to %s:%d", addr_str, addr->port);
  switch (addr->family) {
    case AF_INET6:
      addr->sin6.sin6_family = AF_INET6;
      sa = (struct sockaddr *)&addr->sin6;
      sock_len = sizeof(struct sockaddr_in6);
      break;
    case AF_INET:
    default:
      addr->sin.sin_family = AF_INET;
      sa = (struct sockaddr *)&addr->sin;
      sock_len = sizeof(struct sockaddr_in);
      break;
  }

  if ((ret = sendto(udp_socket->fd, buf, len, 0, sa, sock_len)) < 0) {
    LOGE("Failed to sendto: %s", strerror(errno));
    return -1;
  }

  return ret;
}

int udp_socket_recvfrom(UdpSocket *udp_socket, Address *addr, uint8_t *buf, int len) {

  struct sockaddr_in6 sin6;
  struct sockaddr_in sin;
  struct sockaddr *sa;
  socklen_t sock_len;
  int ret;

  if (udp_socket->fd < 0) {
    LOGE("recvfrom before socket init");
    return -1; 
  }

  switch (udp_socket->bind_addr.family) {
    case AF_INET6:
      sin6.sin6_family = AF_INET6;
      sa = (struct sockaddr *)&sin6;
      sock_len = sizeof(struct sockaddr_in6);
      break;
    case AF_INET:
    default:
      sin.sin_family = AF_INET;
      sa = (struct sockaddr *)&sin;
      sock_len = sizeof(struct sockaddr_in);
      break;
  }

  if ((ret = recvfrom(udp_socket->fd, buf, len, 0, sa, &sock_len)) < 0) {
    LOGE("Failed to recvfrom: %s", strerror(errno));
    return -1;
  }

  if (addr) {
    switch (udp_socket->bind_addr.family) {
      case AF_INET6:
        addr->family = AF_INET6;
        addr->port = htons(addr->sin6.sin6_port);
        memcpy(&addr->sin6, &sin6, sizeof(struct sockaddr_in6));
      break;
    case AF_INET:
    default:
        addr->family = AF_INET;
        addr->port = htons(addr->sin.sin_port);
        memcpy(&addr->sin, &sin, sizeof(struct sockaddr_in));
      break;
    }
  }

  return ret;
}

int tcp_socket_open(TcpSocket *tcp_socket, int family) {

  tcp_socket->bind_addr.family = family;
  switch (family) {
    case AF_INET6:
      tcp_socket->fd = socket(AF_INET6, SOCK_STREAM, 0);
      break;
    case AF_INET:
    default:
      tcp_socket->fd = socket(AF_INET, SOCK_STREAM, 0);
      break;
  }

  if (tcp_socket->fd < 0) {
    LOGE("Failed to create socket");
    return -1;
  }
  return 0;
}

int tcp_socket_connect(TcpSocket *tcp_socket, Address *addr) {

  char addr_string[ADDRSTRLEN];
  int ret;
  struct sockaddr *sa;
  socklen_t sock_len;

  if (tcp_socket->fd < 0) {
    LOGE("Connect before socket init");
    return -1;
  }

  switch (addr->family) {
    case AF_INET6:
      addr->sin6.sin6_family = AF_INET6;
      sa = (struct sockaddr *)&addr->sin6;
      sock_len = sizeof(struct sockaddr_in6);
      break;
    case AF_INET:
    default:
      addr->sin.sin_family = AF_INET;
      sa = (struct sockaddr *)&addr->sin;
      sock_len = sizeof(struct sockaddr_in);
      break;
  }

  addr_to_string(addr, addr_string, sizeof(addr_string));
  LOGI("Connecting to server1 : %s:%d", addr_string, addr->port);
  LOGI("Connecting to server: %s:%d", addr_string, addr->port);
  if ((ret = connect(tcp_socket->fd, sa, sock_len)) < 0) {
    LOGE("Failed to connect to server");
    return -1;
  }

  LOGI("Server is connected");
  return 0;
}

void tcp_socket_close(TcpSocket *tcp_socket) {

  if (tcp_socket->fd > 0) {
    close(tcp_socket->fd);
  }
}

int tcp_socket_send(TcpSocket *tcp_socket, const uint8_t *buf, int len) {

  fd_set write_set;
  struct timeval tv;
  int ret = -1;

  if (tcp_socket->fd < 0) {

    LOGE("sendto before socket init");
    return -1;
  }

  FD_ZERO(&write_set);
  FD_SET(tcp_socket->fd, &write_set);

  tv.tv_sec = 0;
  tv.tv_usec = 500000;

  if ((ret = select(tcp_socket->fd + 1, NULL, &write_set, NULL, &tv)) < 0) {

    LOGE("Failed to select: %s", strerror(errno));
    return -1;
  }

  if (FD_ISSET(tcp_socket->fd, &write_set)) {

    ret = send(tcp_socket->fd, buf, len, 0);
    if (ret < 0) {
      LOGE("Failed to send: %s", strerror(errno));
      return -1;
    }
  }

  return ret;
}

int tcp_socket_recv(TcpSocket *tcp_socket, uint8_t *buf, int len) {

  fd_set read_set;
  struct timeval tv;
  int ret;

  if (tcp_socket->fd < 0) {

    LOGE("recvfrom before socket init");
    return -1;
  }

  FD_ZERO(&read_set);
  FD_SET(tcp_socket->fd, &read_set);
  tv.tv_sec = 0;
  tv.tv_usec = 500000;

  if ((ret = select(tcp_socket->fd + 1, &read_set, NULL, NULL, &tv)) < 0) {

    LOGE("Failed to select: %s", strerror(errno));
    return -1;
  }

  if (FD_ISSET(tcp_socket->fd, &read_set)) {

    ret = recv(tcp_socket->fd, buf, len, 0);
    if (ret < 0) {
      LOGE("Failed to recv: %s", strerror(errno));
      return -1;
    }
  }

  return ret;
}
