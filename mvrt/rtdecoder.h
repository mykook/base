/**
 * @file rtdecoder.h
 *
 * @brief Interface to message decoder.
 */
#ifndef MVRT_DECODER_H
#define MVRT_DECODER_H

#include <mq/mqueue.h>    /* mv_mqueue_t */
#include "rtevqueue.h"

typedef void mvrt_decoder_t;

/* Create a decoder from the given message queue, which will decode a 
   string from the queue, create a MV message, and eventually create
   an event into the event queue. */
extern mvrt_decoder_t *mvrt_decoder(mv_mqueue_t *mq, mvrt_evqueue_t *evq);
extern int mvrt_decoder_run(mvrt_decoder_t *md);

#endif /* MVRT_DECODER_H */
