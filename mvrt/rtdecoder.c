/**
 * @File rtdecoder.c
 */
#include <stdio.h>        /* printf */
#include <stdlib.h>       /* malloc, free */
#include <pthread.h>      /* pthread_create */
#include <signal.h>       /* sigemptyset */
#include <time.h>         /* nanosleep */
#include <assert.h>       /* aasert */
#include <mv/value.h>     /* mv_value_t */
#include <mv/message.h>   /* mv_message_t */
#include "rtprop.h"       /* mvrt_prop_t */
#include "rtfunc.h"       /* mvrt_func_t */
#include "rtdecoder.h"


typedef struct {
  mvrt_evqueue_t *evq;
  pthread_t thr;
} _decoder_t;

static void *_decoder_thread(void *arg);
static mvrt_eventinst_t *_decoder_decode(mv_message_t *mvmsg);


void *_decoder_thread(void *arg)
{
  _decoder_t *mf = (_decoder_t *) arg;      /* decoder */
  mvrt_evqueue_t *evq = mf->evq;            /* event queue */

  char *str ;                               /* message string from mq */
  mv_message_t *mvmsg;                      /* mv_message */
  mvrt_event_t *evt;                        /* event from decoder */
  struct timespec ts;                       /* time for nanosleep */

  mv_value_t null_v = mv_value_null();      /* null value */
  mvrt_eventinst_t *evinst;                 /* event instance */

  ts.tv_sec = 0;
  ts.tv_nsec = 5000000;

  
  while (1) {
    char *str = mv_message_recv();
    if (!str) {
      // sleep for 5ms
      nanosleep(&ts, NULL);
      continue;
    }

    fprintf(stdout, "Message fetched from queue: %s\n", str);
    mvmsg = mv_message_parse(str);
    if (!mvmsg) {
      fprintf(stderr, "Failed to parse message: %s\n", str);
      continue;
    }

    evinst = _decoder_decode(mvmsg);
    if (!evinst) {
      fprintf(stderr, "Failed to decode message: %s\n", str);
      continue;
    }

    free(str);

    while (mvrt_evqueue_full(evq)) {
      nanosleep(&ts, NULL);
    }
    mvrt_evqueue_put(evq, evinst);
  }

  pthread_exit(NULL);
}

enum {
  _V_STRING_NAME   = 0,
  _V_STRING_ARG    = 1,
  _V_STRING_FUNARG = 2,
  _V_STRING_DEV    = 3,
  _V_NTAGS
};
static mv_value_t _values[_V_NTAGS];
static int _decoder_init_done = 0;

void _decoder_init()
{
  if (_decoder_init_done)
    return;

  _values[_V_STRING_NAME] = mv_value_string("name");
  _values[_V_STRING_ARG] = mv_value_string("arg");
  _values[_V_STRING_FUNARG] = mv_value_string("funarg");
  _values[_V_STRING_FUNARG] = mv_value_string("dev");
  _decoder_init_done = 1;
}

mvrt_eventinst_t *_decoder_decode(mv_message_t *mvmsg)
{
  mvrt_eventinst_t *evinst;     /* event instance */

  mv_value_t arg_v;             /* arg field value of the message */
  mv_value_t src_v;             /* src field value of the message */
  mv_value_t name_v;            /* name */
  mv_value_t dev_v;             /* dev */

  mvrt_event_t *event;          /* event type */
  mvrt_prop_t *prop;            /* property */
  mv_value_t pair_v;            /* pair value */
  mv_value_t null_v;            /* null value */

  char *name_s;                 /* name string */
  char *dev_s;                  /* dev string */

  switch (mvmsg->tag) {
  case MV_MESSAGE_EVENT_OCCUR:
    /* 
       { 
         tag: "EVENT_OCCUR",
         arg: { "name": "adjust_volume", "volume": 12 } 
       }
    */
    arg_v = mvmsg->arg;
    src_v = mvmsg->src;
    name_v = mv_value_map_lookup(arg_v, _values[_V_STRING_NAME]);
    name_s = mv_value_string_get(name_v);
    dev_v = mv_value_map_lookup(src_v, _values[_V_STRING_DEV]);
    dev_s = mv_value_string_get(dev_v);
    event = mvrt_event_lookup(dev_s, name_s);
    if (!event) {
      fprintf(stderr, "Failed to find the event handle for %s.\n", name_s);
      return NULL;
    }
    evinst = mvrt_eventinst_new(event, arg_v);
    return evinst;
  case MV_MESSAGE_PROP_ADD:
    /* 
       { 
         tag: "PROP_ADD",
         arg: { "name": "current_volume" } 
       }
    */
    arg_v = mvmsg->arg;
    name_v = mv_value_map_lookup(arg_v, _values[_V_STRING_NAME]);
    name_s = mv_value_string_get(name_v);
    prop = mvrt_prop_new(name_s);
    if (!prop) {
      fprintf(stderr, "ERROR: Failed to create property: %s.\n", name_s);
      return NULL;
    }
    break;
  case MV_MESSAGE_PROP_SET:
    arg_v = mvmsg->arg;
    event = mvrt_event_lookup("_E_prop_set", NULL);
    evinst = mvrt_eventinst_new(event, arg_v);
    return evinst;
  case MV_MESSAGE_PROP_GET:
    arg_v = mvmsg->arg;
    event = mvrt_event_lookup("_E_prop_get", NULL);
    evinst = mvrt_eventinst_new(event, arg_v);
    return evinst;
  case MV_MESSAGE_FUNC_CALL:
    arg_v = mvmsg->arg;
    event = mvrt_event_lookup("_E_func_call", NULL);
    evinst = mvrt_eventinst_new(event, arg_v);
    return evinst;
  case MV_MESSAGE_REPLY:
    arg_v = mvmsg->arg;
    event = mvrt_event_lookup("_E_reply", NULL);
    evinst = mvrt_eventinst_new(event, arg_v);
    return evinst;
  default:
    break;
  }

  assert(0 && "Unimplemented or invalid message tag in _decoder_decode");
  return NULL;
}

/*
 * Functions for the decoder API.
 */
mvrt_decoder_t *mvrt_decoder(mvrt_evqueue_t *evq)
{
  _decoder_init();

  _decoder_t *mf = (_decoder_t *) malloc(sizeof(_decoder_t));
  if (!mf)
    return NULL;

  mf->evq = evq;

  return mf;
}

int mvrt_decoder_run(mvrt_decoder_t *f)
{
  _decoder_t *mf = (_decoder_t *) f;

  sigset_t sigmask;
  sigemptyset(&sigmask);
  sigaddset(&sigmask, SIGRTMIN);
  if (pthread_sigmask(SIG_BLOCK, &sigmask, NULL) != 0) {
    perror("pthread_sigmask@mvrt_decoder_run");
    return -1;
  }

  if (pthread_create(&mf->thr, NULL, _decoder_thread, mf) != 0) {
    perror("pthread_create@mvrt_decoder_run");
    return -1;
  }
  return 0;
}


