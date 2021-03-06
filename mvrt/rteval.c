/**
 * @file rteval.c
 */
#include <stdio.h>       /* fprintf */
#include <stdlib.h>      /* malloc */
#include <string.h>      /* strstr */
#include <dlfcn.h>       /* dlopen */
#include <assert.h>      /* assert */
#include <mv/message.h>  /* mv_message_send */
#include <mv/device.h>   /* mv_device_t */
#include "rtprop.h"      /* mvrt_prop_t */
#include "rtfunc.h"      /* mvrt_func_t */
#include "rtcontext.h"   /* mvrt_stack_t */
#include "rteval.h"


static int _eval(mvrt_code_t *code, mvrt_context_t *ctx);
static int _eval_instr(mvrt_instr_t *instr, mvrt_context_t *ctx);
static int _eval_arithmetic(mvrt_instr_t *instr, mvrt_context_t *ctx);
static int _eval_branch(mvrt_instr_t *instr, mvrt_context_t *ctx);
static int _eval_push(mvrt_instr_t *instr, mvrt_context_t *ctx);
static int _eval_cons(mvrt_instr_t *instr, mvrt_context_t *ctx);
static int _eval_stackop(mvrt_instr_t *instr, mvrt_context_t *ctx);
static int _eval_call_func(mvrt_instr_t *instr, mvrt_context_t *ctx);
static int _eval_call_native(mvrt_func_t *f, mv_value_t a, mvrt_context_t *c);
static int _eval_call_return(mvrt_instr_t *instr, mvrt_context_t *ctx);
static int _eval_call_continue(mvrt_instr_t *instr, mvrt_context_t *ctx);
static char *_eval_getdev(char *s);
static char *_eval_getname(char *s);

extern char *dest;

typedef enum {
  _EVAL_FAILURE = -1,
  _EVAL_SUSPEND = -2,
  _EVAL_RETURN  = -3
}  _eval_ret_t;

int _eval(mvrt_code_t *code, mvrt_context_t* ctx)
{
  mvrt_instr_t *instr = code->instrs + ctx->iptr;
  mvrt_continue_t *cont = NULL;
  int next_ip = 0;
  while (instr && ctx->iptr < code->size) {
    const char *opstr = mvrt_opcode_str(instr->opcode);


      fprintf(stdout, "\tEVAL[%2d]: %s\n", ctx->iptr, opstr);

    next_ip = _eval_instr(instr, ctx);
    if (next_ip == _EVAL_FAILURE) {
      fprintf(stderr, "Evaluation error at %s.\n", opstr);
      break;
    }
    else if (next_ip == _EVAL_SUSPEND) {
      break;
    }
    else if (next_ip == _EVAL_RETURN) {
      break;
    }
    ctx->iptr = next_ip;
    instr = code->instrs + ctx->iptr;
  }

  return next_ip;
}

/* Returns the next IP >= 0 on normal situation. Returns _EVAL_FAILURE on error.
   Returns _EVAL_SUSPEND when we execution was suspened. Retuens _EVAL_RETURN
   when "ret" operator was evaluated. */
int _eval_instr(mvrt_instr_t *instr, mvrt_context_t *ctx)
{
  mvrt_stack_t *stack = ctx->stack;
  int ip = ctx->iptr;

  int retval;
  switch (instr->opcode) {
  /* arithmetic */
  case MVRT_OP_ADD:
  case MVRT_OP_SUB:
  case MVRT_OP_MUL:
  case MVRT_OP_DIV:
    return _eval_arithmetic(instr, ctx);

  /* flow control */
  case MVRT_OP_JMP:
  case MVRT_OP_BEQ:
  case MVRT_OP_RET:
    return _eval_branch(instr, ctx);
  case MVRT_OP_PUSHN:
  case MVRT_OP_PUSH0:
  case MVRT_OP_PUSH1:
  case MVRT_OP_PUSHI:
  case MVRT_OP_PUSHS:
    return _eval_push(instr, ctx);
  case MVRT_OP_POP:
    mvrt_stack_pop(stack);
    return ip + 1;

  /* value builder */
  case MVRT_OP_CONS_NEW:
  case MVRT_OP_CONS_CAR:
  case MVRT_OP_CONS_CDR:
  case MVRT_OP_CONS_SETCAR:
  case MVRT_OP_CONS_SETCDR:
    return _eval_cons(instr, ctx);

  /* load/save to/from stack */
  case MVRT_OP_GETARG:
  case MVRT_OP_GETF:
  case MVRT_OP_SETF:
    return _eval_stackop(instr, ctx);

  /* properties */
  case MVRT_OP_PROP_GET:
    return _eval_prop_get(instr, ctx);
  case MVRT_OP_PROP_SET:
    return _eval_prop_set(instr, ctx);

  /* function calls */
  case MVRT_OP_CALL_FUNC:
  case MVRT_OP_CALL_FUNC_RET:
    return _eval_call_func(instr, ctx);
  case MVRT_OP_CALL_RETURN:
    return _eval_call_return(instr, ctx);
  case MVRT_OP_CALL_CONTINUE:
    return _eval_call_continue(instr, ctx);
  default:
    break;
  }

  assert(0 && "Either OP not implemented or something wrong.");
  return ip + 1;
}

int _eval_arithmetic(mvrt_instr_t *instr, mvrt_context_t *ctx)
{
  mvrt_stack_t *stack = ctx->stack;
  int ip = ctx->iptr;

  mv_value_t val0_v = mvrt_stack_pop(stack);
  mv_value_t val1_v = mvrt_stack_pop(stack);
  int val0 = mv_value_int_get(val0_v);
  int val1 = mv_value_int_get(val1_v);
  int result;
  switch (instr->opcode) {
  case MVRT_OP_ADD:
    result = val0 + val1;
    break;
  case MVRT_OP_SUB:
    result = val0 - val1;
    break;
  case MVRT_OP_MUL:
    result = val0 * val1;
    break;
  case MVRT_OP_DIV:
    if (val1 == 0) {
      assert(0 && "Divided by 0 error.");
    }
    result = val0 / val1;
    break;
  default:
    assert(0 && "Invalid opcode.");
    break;
  }

  mv_value_t result_v = mv_value_int(result);
  mvrt_stack_push(stack, result_v);

  return ip + 1;
}

int _eval_branch(mvrt_instr_t *instr, mvrt_context_t *ctx)
{
  mvrt_stack_t *stack = ctx->stack;
  int ip = ctx->iptr;

  /* 
     BEQ <branch ip>

     Pop two scalar values from the stack and compare. If they are equal,
     jump to <branch ip>. Otherwise, just increase the ip by 2.
   */
  int jmpval;
  mv_value_t val0;
  mv_value_t val1;
  switch (instr->opcode) {
  case MVRT_OP_JMP:
    jmpval = (int) instr->ptr;
    return jmpval;
  case MVRT_OP_BEQ:
    jmpval = (int) instr->ptr;
    val0 = mvrt_stack_pop(stack);
    val1 = mvrt_stack_pop(stack);
    if (mv_value_eq(val0, val1))
      return jmpval;
    break;
  case MVRT_OP_RET:
    return _EVAL_RETURN;
  default:
    break;
  }

  return ip + 1;
}

int _eval_push(mvrt_instr_t *instr, mvrt_context_t *ctx)
{
  mvrt_stack_t *stack = ctx->stack;
  int ip = ctx->iptr;

  /*
    PUSH <arg>

    Transforms scalar arg (which is int, char *, etc.) into mv_value_t
    and push it into stack.
  */

  switch (instr->opcode) {
  case MVRT_OP_PUSHN:
    {
      mvrt_stack_push(stack, mv_value_null());
    }
    break;
  case MVRT_OP_PUSHI:
    {
      int intval = (int) instr->ptr;
      mv_value_t intval_v = mv_value_int(intval);
      mvrt_stack_push(stack, intval_v);
    }
    break;
  case MVRT_OP_PUSHS:
    {
      char *strval = (char *) instr->ptr;
      mv_value_t str_v = mv_value_string(strval);
      mvrt_stack_push(stack, str_v);
    }
    break;
  default:
    return _EVAL_FAILURE;
  }
  return ip + 1;
}

int _eval_cons(mvrt_instr_t *instr, mvrt_context_t *ctx)
{
  mvrt_stack_t *stack = ctx->stack;
  int ip = ctx->iptr;
  
  mv_value_t val0;
  mv_value_t val1;
  mv_value_t cons;
  switch (instr->opcode) {
  case MVRT_OP_CONS_NEW:
    cons = mv_value_cons();
    val0 = mvrt_stack_pop(stack);
    val1 = mvrt_stack_pop(stack);
    mv_value_cons_setcar(cons, val0);
    mv_value_cons_setcdr(cons, val1);
    mvrt_stack_push(stack, cons);
    break;
  case MVRT_OP_CONS_CAR:
    cons = mvrt_stack_pop(stack);
    val0 = mv_value_cons_car(cons);
    mvrt_stack_push(stack, val0);
    break;
  case MVRT_OP_CONS_CDR:
    cons = mvrt_stack_pop(stack);
    val0 = mv_value_cons_cdr(cons);
    mvrt_stack_push(stack, val0);
    break;
  case MVRT_OP_CONS_SETCAR:
    cons = mvrt_stack_pop(stack);
    val0 = mvrt_stack_pop(stack);
    mv_value_cons_setcar(cons, val0);
    mvrt_stack_push(stack, cons);
    break;
  case MVRT_OP_CONS_SETCDR:
    cons = mvrt_stack_pop(stack);
    val0 = mvrt_stack_pop(stack);
    mv_value_cons_setcdr(cons, val0);
    mvrt_stack_push(stack, cons);
    break;
  default:
    assert(0 && "Invalid OPCODE.");
    break;
  }

  return ip + 1;
}

int _eval_stackop(mvrt_instr_t *instr, mvrt_context_t *ctx)
{
  mvrt_stack_t *stack = ctx->stack;
  int ip = ctx->iptr;

  mv_value_t val0;
  mv_value_t val1;
  mv_value_t val2;
  mv_value_t val3;
  switch (instr->opcode) {
  case MVRT_OP_GETARG:
    val0 = ctx->arg;
    mvrt_stack_push(stack, val0);
    break;
  case MVRT_OP_GETF:
    val0 = mvrt_stack_pop(stack);
    val1 = mvrt_stack_pop(stack);
    val2 = mv_value_map_lookup(val1, val0);
    mvrt_stack_push(stack, val2);
    break;
  case MVRT_OP_SETF:
    val0 = mvrt_stack_pop(stack);
    val1 = mvrt_stack_pop(stack);
    val2 = mvrt_stack_pop(stack);
    val3 = mv_value_map_add(val2, val0, val1);
    mvrt_stack_push(stack, val3);
    break;
  default:
    assert(0 && "Not implemented yet");
    break;
  }

  return ip + 1;
}

int _eval_prop_get(mvrt_instr_t *instr, mvrt_context_t *ctx)
{
  mvrt_stack_t *stack = ctx->stack;
  int ip = ctx->iptr;

  mv_value_t prop_v = mvrt_stack_pop(stack);
  char *prop_s = mv_value_string_get(prop_v);

  char *dev_s = _eval_getdev(prop_s);
  char *name_s = _eval_getname(prop_s);

  if (dev_s) {
    static char arg[4096];
    const char *destaddr = mv_device_addr(dev_s);
    int retid = mvrt_continuation_new(ctx);
    sprintf(arg, "{\"name\":\"%s\", \"retid\":%d, \"retaddr\":\"%s\"}",
            name_s, retid, mv_message_selfaddr());
    fprintf(stdout, "MQSEND: %s\n", arg);
    mv_message_send(destaddr, MV_MESSAGE_PROP_GET, arg);
    
    free(dev_s);
    return _EVAL_SUSPEND;
  }
  else {
    /* local prop */
    mvrt_prop_t *mvprop = mvrt_prop_lookup(name_s);
    if (mvprop) {
      mv_value_t value_v = mvrt_prop_getvalue(mvprop);
      mvrt_stack_push(stack, value_v);
      
#if 0
      fprintf(stdout, "\t=> PROP_GET: "); mv_value_print(value_v);
#endif
    }
    else {
      /* failed to find the property; process error */
      mv_value_t value_v = mv_value_string("E:NO_SUCH_PROP");
      mvrt_stack_push(stack, value_v);
    }
    return ip + 1;
  }
}

int _eval_prop_set(mvrt_instr_t *instr, mvrt_context_t *ctx)
{
  mvrt_stack_t *stack = ctx->stack;
  int ip = ctx->iptr;

  mv_value_t prop_v = mvrt_stack_pop(stack);
  mv_value_t value_v = mvrt_stack_pop(stack);
  char *prop = mv_value_string_get(prop_v);
  mvrt_prop_t *mvprop = mvrt_prop_lookup(prop);
  assert(prop && "No such property exists.");
  
  if (mvrt_prop_setvalue(mvprop, value_v) == -1) {
    fprintf(stderr, "PROP_SET failed: %s.\n", prop);
    return _EVAL_FAILURE;
  }

  return ip + 1;
}

int _eval_call_local(mvrt_instr_t *instr, mvrt_context_t *ctx)
{
  mvrt_stack_t *stack = ctx->stack;
  int ip = ctx->iptr;

  assert(0 && "Not implemented yet");

  return _EVAL_FAILURE;
}

int _eval_call_func(mvrt_instr_t *instr, mvrt_context_t *ctx)
{
  mvrt_stack_t *stack = ctx->stack;
  int ip = ctx->iptr;
  
  /*
     CALL_FUNC[_RET]

     Call a remote function with/without waiting for the return value. Stack
     top should contain function value, number of arguments, and arguments.
   */
  mv_value_t fnam_v = mvrt_stack_pop(stack);
  mv_value_t farg_v = mvrt_stack_pop(stack);

  char *func_s = (char *) mv_value_string_get(fnam_v);

  char *dev_s = _eval_getdev(func_s);
  char *name_s = _eval_getname(func_s);

  if (!dev_s) {
    mvrt_func_t *mvfunc = mvrt_func_lookup(name_s);
    if (mvfunc) {
      /* local function */
      if (mvrt_func_isnative(mvfunc)) {
        return _eval_call_native(mvfunc, farg_v, ctx);
      }
      else {
        assert(0 && "MV func not implemented yet");
        return ip + 1;
      }
    }
    else {
      assert(0 && "Local function with the given name not found");
      return ip + 1;
    }
  }

  const char *destaddr = mv_device_addr(dev_s);
  mvrt_native_t *native = NULL;

  static char arg[4096];
  int retid;

  switch (instr->opcode) {
  case MVRT_OP_CALL_FUNC:
    sprintf(arg, " {\"name\":\"%s\", \"funarg\":%s }",
            name_s, mv_value_to_str(farg_v));
    fprintf(stdout, "MQSEND FUNC_CALL: %s\n", arg);
    mv_message_send(destaddr, MV_MESSAGE_FUNC_CALL, arg);
    break;
  case MVRT_OP_CALL_FUNC_RET:
    retid = mvrt_continuation_new(ctx);
    sprintf(arg, "{\"name\":\"%s\", \"funarg\":%s, \"retid\":%d, "
            "  \"retaddr\": \"%s\" }",
            name_s, mv_value_to_str(farg_v), retid, mv_message_selfaddr());
    fprintf(stdout, "MQSEND: FUNC_CALL_RET %s\n", arg);
    mv_message_send(destaddr, MV_MESSAGE_FUNC_CALL_RET, arg);
    free(dev_s);
    return _EVAL_SUSPEND;
  default:
    assert(0 && "Must not reach here");
    break;
  }

  free(dev_s);
  return ip + 1;
}

int _eval_call_native(mvrt_func_t *f, mv_value_t farg_v,  mvrt_context_t *ctx)
{
  mvrt_stack_t *stack = ctx->stack;
  int ip = ctx->iptr;

  mvrt_native_t *native = mvrt_func_getnative(f);

  /* get function */
  if (!native->func1) {
    void *handle = dlopen(native->lib, RTLD_NOW);
    if (!handle) {
      fprintf(stderr, "%s\n", dlerror());
      exit(EXIT_FAILURE);
    }
    char *name = native->name;
    native->func1 = (mvrt_native_func1_t) dlsym(handle, name);
    native->func2 = (mvrt_native_func2_t) dlsym(handle, name);
  }

  /* prepare args */
  int nargs = 0;
  int arg1 = 0;
  int arg2 = 0;
  if (mv_value_tag(farg_v) == MV_VALUE_CONS) {
    /* two integers */
    nargs = 2;
    mv_value_t car = mv_value_cons_car((mv_value_t) farg_v);
    mv_value_t cdr = mv_value_cons_cdr((mv_value_t) farg_v);
    mv_value_t cdar = mv_value_cons_car((mv_value_t) cdr);
    arg2 = mv_value_int_get(car);
    arg1 = mv_value_int_get(cdar);
    fprintf(stdout, "arg1 = %d, arg2 = %d\n", arg1, arg2);
    
  }
  else {
    /* integer */
    nargs = 1;
    arg1 = mv_value_int_get(farg_v);
    fprintf(stdout, "arg1 = %d\n", arg1);
  }

  /* call function */
  int retval = 0;
  if (nargs == 1) {
    retval = (native->func1)(arg1);
  }
  else
    (native->func2)(arg1, arg2);

  mv_value_t retval_v = mv_value_int(retval);
  mvrt_stack_push(stack, retval_v);

  return ip + 1;
}

int _eval_call_return(mvrt_instr_t *instr, mvrt_context_t *ctx)
{
  mvrt_stack_t *stack = ctx->stack;
  int ip = ctx->iptr;

  mv_value_t retid_v = mvrt_stack_pop(stack);
  mv_value_t retaddr_v = mvrt_stack_pop(stack);
  mv_value_t retval_v = mvrt_stack_pop(stack);

  int retid = mv_value_int_get(retid_v);
  const char *retaddr = mv_value_string_get(retaddr_v);

  static char arg[4096];
  sprintf(arg, "{\"retid\":%d, \"retval\":%s}",
          retid, mv_value_to_str(retval_v));
  fprintf(stdout, "REPLY: %s\n", arg);
  mv_message_send(retaddr, MV_MESSAGE_REPLY, arg);

  return ip + 1;
}

int _eval_call_continue(mvrt_instr_t *instr, mvrt_context_t *ctx)
{
  mvrt_stack_t *stack = ctx->stack;
  int ip = ctx->iptr;

  mv_value_t retid_v = mvrt_stack_pop(stack);
  mv_value_t retval_v = mvrt_stack_pop(stack);
  int retid = mv_value_int_get(retid_v);

  mvrt_continue_t *cont = mvrt_continuation_get(retid);
  mvrt_context_t *cont_ctx = cont->ctx;

  mvrt_stack_push(cont_ctx->stack, retval_v);

  _eval(cont_ctx->code, cont_ctx);

  return ip + 1;
}

/* Returns the first part of "dev:name". Caller is responsible to deallocate
   the returned string. */
char *_eval_getdev(char *s)
{
  char *charp = strstr(s, ":");
  if (!charp)
    return NULL;

  if (charp == s)
    return NULL;

  char save = *charp;
  *charp = '\0';
  char *retstr = strdup(s);
  *charp = save;
  
  return retstr;
}

/* Returns the second part of "dev:name". */
char *_eval_getname(char *s)
{
  char *charp = strstr(s, ":");
  if (charp)
    return charp + 1;
  
  return s;
}


/*
 * Functions for the eval interface.
 */
int mvrt_eval_reactor(mvrt_reactor_t *reactor, mvrt_eventinst_t *evinst)
{
  mvrt_event_t *evtype = evinst->type;
  mv_value_t evdata = evinst->data;

  /* Create a new context
     . code
     . iptr = 0
     . stack is newly created with the evdata as its only element
  */
  mvrt_context_t *ctx = mvrt_context_new(mvrt_reactor_getcode(reactor));
  ctx->iptr = 0;
  ctx->stack = mvrt_stack_new();
  ctx->arg = evdata;
  mvrt_stack_push(ctx->stack, evdata);
  
  int retval = _eval(ctx->code, ctx);
  
  if (retval != _EVAL_SUSPEND)
    mvrt_context_delete(ctx);
  
  return retval;
}

