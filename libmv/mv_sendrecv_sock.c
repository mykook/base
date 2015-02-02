/**
 * @file mv_sendrecv_sock.c
 *
 * @brief Implementation of sendrecv functions using Unix sockets.
 */
#include <stdio.h>       /* sprintf */
#include <stdlib.h>      /* malloc */
#include <string.h>      /* memcpy */
#include <time.h>        /* nanosleep */
#include <errno.h>       /* errno */
#include <pthread.h>     /* pthread_create */
#include <signal.h>      /* sigemptyset */
#include <assert.h>      /* assert */
#include <sys/socket.h>  /* inet_nota */
#include <sys/types.h>   /* getifaddrs */
#include <netinet/in.h>  /* sockaddr_in */
#include <arpa/inet.h>   /* inet_nota */
#include <ifaddrs.h>     /* getifaddrs */
#include <mv/device.h>   /* mv_device_self */
#include <mv/message.h>  /* mv_message_send */
#include "mv_netutil.h"  /* mv_writemsg */

#define MAX_MESSAGE_QUEUE 4096
typedef struct _mq {
  pthread_mutex_t lock;
  int size;
  int head;
  int tail;
  char **msgs;
} _mq_t;

static _mq_t *_mq_new(int size);
static int _mq_delete(_mq_t *mq);
static int _mq_enqueue(_mq_t *mq, char *s);
static char *_mq_dequeue(_mq_t *mq);
static void *_mq_input_thread(void *arg);
static void *_mq_output_thread(void *arg);
static const char *_mq_selfaddr();
static const char *_mq_getaddr(const char *str);
static const char *_mq_getdata(const char *str);


/* 
 * Message layer info structure.
 */
typedef struct _mqinfo {
  struct sockaddr_in servaddr;  /* server address */
  char *addr;                   /* address string for input queue */
  char *srcstr;                 /* {"dev": "mydev", "addr": "..."} */

  void *ctx;                    /* zmq context */
  int sock;                     /* REP (server) socket */

  pthread_t thr_rep;            /* REP (server) thread for input queue */
  pthread_t thr_req;            /* REQ (client) thread for output queue */

  _mq_t *imq;                   /* input message qeueue */
  _mq_t *omq;                   /* output message qeueue */
} _mqinfo_t;
static _mqinfo_t *_mqinfo_init(unsigned port);
static _mqinfo_t *_mqinfo_get();
static int _mqinfo_run(_mqinfo_t *mqinfo);


_mq_t *_mq_new(int size)
{
  if (size > MAX_MESSAGE_QUEUE) {
    fprintf(stdout, "Max message queue size is %d.\n", MAX_MESSAGE_QUEUE);
    size = MAX_MESSAGE_QUEUE;
  }
  _mq_t *mq = malloc(sizeof(_mq_t));
  pthread_mutex_init(&mq->lock, NULL);
  mq->size = size;
  mq->head = 0;
  mq->tail = 0;
  mq->msgs = malloc(sizeof(char *) * size);

  return mq;
}

int _mq_delete(_mq_t *mq)
{
  free(mq->msgs);
  free(mq);

  return 0;
}

int _mq_full(_mq_t *mq)
{
  return (mq->tail + 1) % mq->size == mq->head;
}

int _mq_empty(_mq_t *mq)
{
  return mq->tail == mq->head;
}

int _mq_enqueue(_mq_t *mq, char *s)
{
  if (pthread_mutex_lock(&mq->lock) != 0) {
    perror("pthread_mutex_lock@_mq_enqueue");
    return -1;
  }

  if (_mq_full(mq)) {
    if (pthread_mutex_unlock(&mq->lock) != 0) {
      perror("pthread_mutex_unlock@_mq_enqueue");
    }
    return -1;
  }

  mq->msgs[mq->tail] = s;
  mq->tail = (mq->tail + 1) % mq->size;

  if (pthread_mutex_unlock(&mq->lock) != 0)  {
    perror("pthread_mutex_unlock@_mq_enqueue");
    return -1;
  }

  return 0;
}

char *_mq_dequeue(_mq_t *mq)
{
  pthread_mutex_lock(&mq->lock);
  if (_mq_empty(mq)) {
    pthread_mutex_unlock(&mq->lock);
    return NULL;
  }

  char *s = mq->msgs[mq->head];
  mq->head = (mq->head + 1) % mq->size;

  pthread_mutex_unlock(&mq->lock);
  return s;
}

#define LISTENQ_SIZE 128
void *_mq_input_thread(void *arg)
{
  _mqinfo_t *mq = (_mqinfo_t *) arg;  /* message queue */
  struct timespec ts;                 /* time for nanosleep */
  int connfd;                         /* connected descriptor */
  int recvsz;                         /* size of received message */
  char *recvbuf;                      /* pointer to received message */
  char *recvstr;                      /* copy of received message */

  ts.tv_sec = 0;
  ts.tv_nsec = 1000;

  printf("listen\n");
  if (listen(mq->sock, LISTENQ_SIZE) == -1) {
    perror("listen@_mq_input_thread");
    exit(1);
  }

  while (1) {

    printf("accept\n");
    if ((connfd = accept(mq->sock, (SA *) NULL, NULL)) == -1) {
      perror("accept@_mq_input_thread");
      continue;
    }

    printf("read\n");
    if ((recvsz = read(connfd, &recvbuf)) == -1) {
      perror("read@_mq_input_thread");
      continue;
    }
     
    recvstr = malloc(recvsz + 1);
    memcpy(recvstr, (char *) recvbuf, recvsz);
    recvstr[recvsz] = '\0';

#if 0
    fprintf(stdout, "Message received: [%s]:%d\n", recvstr, recvsz);
#endif
    
    while (_mq_enqueue(mq->imq, recvstr) != 0) {
      nanosleep(&ts, NULL);
    }

    if (close(connfd) == -1) {
      perror("close@_mq_input_thread");
      continue;
    }
  }

  pthread_exit(NULL);
}

void *_mq_output_thread(void *arg)
{
  _mqinfo_t *mq = (_mqinfo_t *) arg;  /* message queue */

  struct timespec ts;                 /* time for nanosleep */
  struct sockaddr_in servaddr;        /* server addr */
  char *sendstr;                      /* message to send */

  ts.tv_sec = 0;
  ts.tv_nsec = 1000;

  while (1) {

    if ((sendstr = _mq_dequeue(mq->omq)) == NULL) {
      nanosleep(&ts, NULL);
      continue;
    }

    const char *sendaddr = _mq_getaddr(sendstr);
    const char *senddata = _mq_getdata(sendstr);

#if 1
    fprintf(stdout, "Message to %s: %s\n", sendaddr, senddata);
#endif

    char *addr = strstr(sendaddr, "//") + 2;
    char *port = strstr(addr, ":");

    char *ipaddr = strdup(addr);
    ipaddr[port-addr] = '\0';
  
    printf("ipaddr: %s\n", ipaddr);

    int sock;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
      perror("socket@_mq_output_thread");
      continue;
    }
    printf("sock\n");

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(atoi(port));
    if (inet_pton(AF_INET, ipaddr, &servaddr.sin_addr) == -1) {
      perror("inet_pton@_mq_output_thread");
      continue;
    }
    if (connect(sock, (SA *) &servaddr, sizeof(servaddr)) < 0) {
      perror("connect@_mq_output_thread");
      continue;
    }
    printf("connect\n");

    mv_writemsg(sock, senddata);

    close(sock);
    free(sendstr);
  }
}

const char *_mq_selfaddr()
{
  static char addr_eth[1024];
  static char addr_wlan[1024];

  struct ifaddrs *ifaddr;
  struct ifaddrs *ifa;

  if (getifaddrs(&ifaddr) == -1) {
    perror("getifaddrs@mqutil_getaddr");
    return NULL;
  }

  for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL)
      continue;
    
    if (ifa->ifa_addr->sa_family != AF_INET)
      continue;

    struct sockaddr_in *paddr = (struct sockaddr_in *) ifa->ifa_addr;
    if (!strcmp(ifa->ifa_name, "eth0"))
      strcpy(addr_eth, inet_ntoa(paddr->sin_addr));
    if (!strcmp(ifa->ifa_name, "wlan0"))
      strcpy(addr_wlan, inet_ntoa(paddr->sin_addr));
      
  }
  
  freeifaddrs(ifaddr);

  if (strcmp(addr_eth, ""))
    return &addr_eth[0];

  if (strcmp(addr_wlan, ""))
    return &addr_wlan[0];

  return NULL;
}

const char *_mq_getaddr(const char *str)
{
  static char addr[1024];
  char *data = strstr(str, "{");

  unsigned len = (unsigned long) data - (unsigned long) str;
  strncpy(addr, str, len);
  addr[len] = '\0';

  return &addr[0];
}

const char *_mq_getdata(const char *str)
{
  return strstr(str, "{");
}

_mqinfo_t *_mqinfo_init(unsigned port)
{
  _mqinfo_t *mq = malloc(sizeof(_mqinfo_t));
  mq->imq = _mq_new(MAX_MESSAGE_QUEUE);
  mq->omq = _mq_new(MAX_MESSAGE_QUEUE);

  char s[1024];
  const char *selfaddr = _mq_selfaddr();
  if (!selfaddr) {
    fprintf(stderr, "_mqinfo_init: Failed to get the IP address of host.\n");
    exit(1);
  }
  
  sprintf(s, "tcp://%s:%d", _mq_selfaddr(), port);
  mq->addr = strdup(s);

  const char *dev_s = mv_device_self();
  sprintf(s, "{\"dev\":\"%s\", \"addr\":\"%s\"}", dev_s, mq->addr);
  mq->srcstr = strdup(s);


  /* create server socket for incoming messages */
  if ((mq->sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket@_mqinfo_init");
    exit(1);
  }
  
  bzero(&mq->servaddr, sizeof(mq->servaddr));
  mq->servaddr.sin_family = AF_INET;
  mq->servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  mq->servaddr.sin_port = htons(port);
  
  if (bind(mq->sock, (SA *) &mq->servaddr, sizeof(mq->servaddr)) == -1) {
    perror("bind@_mqinfo_init");
    exit(1);
  }

  return mq;
}

int _mqinfo_run(_mqinfo_t *mqinfo) 
{
  /* SIGRTMIN will be used by runtime in interval timers. Block this
     signal in mq threads. */
  sigset_t sigmask;
  sigemptyset(&sigmask);
  sigaddset(&sigmask, SIGRTMIN);
  if (pthread_sigmask(SIG_BLOCK, &sigmask, NULL) != 0) {
    perror("pthread_sigmask@_mqinfo_run");
    return -1;
  }
  
  if (pthread_create(&mqinfo->thr_rep, NULL, _mq_input_thread, mqinfo) != 0) {
    perror("pthread_create@_mqinfo_run");
    return -1;
  }

  if (pthread_create(&mqinfo->thr_req, NULL, _mq_output_thread, mqinfo) != 0) {
    perror("pthread_create@m_mqinfo_run");
    return -1;
  }

  return 0;
}

#define ZMQ_DEFAULT_PORT 5557
static unsigned _mqport = ZMQ_DEFAULT_PORT;
static _mqinfo_t *_mqinfo = NULL;
_mqinfo_t *_mqinfo_get()
{
  if (!_mqinfo) {
    /* create an mqinfo and initialize the queues when this function
       is first called. */
    if ((_mqinfo = _mqinfo_init(_mqport)) == NULL)
      return NULL;

    if (_mqinfo_run(_mqinfo) == -1)
      return NULL;
  }

  return _mqinfo;
}

/*
 * Implementation of send and recv functions.
 */
int mv_message_send(const char *adr, mv_mtag_t tag, char *arg_s)
{
  _mqinfo_t *mqinfo = _mqinfo_get();
  if (!mqinfo)
    return -1;

  const char *tag_s = mv_message_tagstr(tag);
  char *src_s = _mqinfo->srcstr;
  int sz = strlen(arg_s);
  char *m = malloc(sz + 1024);
  sprintf(m, "%s {\"tag\":\"%s\", \"arg\":%s, \"src\":%s}", 
          adr, tag_s, arg_s, src_s);

  while (_mq_enqueue(mqinfo->omq, m) != 0) ;

  return 0;
}

int mv_message_send_value(const char *adr, mv_mtag_t tag, mv_value_t arg)
{
  _mqinfo_t *mqinfo = _mqinfo_get();
  if (!mqinfo)
    return -1;

  const char *tag_s = mv_message_tagstr(tag);
  char *arg_s = mv_value_to_str(arg);
  char *src_s = _mqinfo->srcstr;
  int sz = strlen(arg_s);
  char *m = malloc(sz + 1024);
  sprintf(m, "%s {\"tag\":%s,\"arg\":%s,\"src\":%s}", adr, tag_s, arg_s, src_s);
  
  while (_mq_enqueue(mqinfo->omq, m) != 0) ;

  return 0;
}

char *mv_message_recv()
{
  _mqinfo_t *mqinfo = _mqinfo_get();
  if (!mqinfo)
    return NULL;

  char *s = NULL;
  while ((s = _mq_dequeue(mqinfo->imq)) == NULL) ;

  return s;
}

const char *mv_message_selfaddr()
{
  _mqinfo_t *mq = _mqinfo_get();

  return mq->addr;
}

int mv_message_setport(unsigned port)
{
  _mqport = port;
  return 0;
}
