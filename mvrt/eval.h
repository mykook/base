/**
 * @file eval.h
 *
 * @brief Interface to evaluation functions.
 */
#ifndef MV_EVAL_H
#define MV_EVAL_H

#include "rtevent.h"
#include "rtreactor.h"

/* Evalute the given reactor. */
extern int mvrt_eval_reactor(mvrt_reactor_t *reactor, mvrt_eventinst_t *ev);

#endif /* MV_EVAL_H */
