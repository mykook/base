/**
 * @file repl.c
 *
 * @brief Read-eval-print loop.
 */
#include <stdio.h>     /* fprintf */
#include <string.h>    /* strchr */
#include "command.h"   /* mvsh_command_process */
#include "repl.h"      /* mvsh_repl */

#define MVSH_PROMPT     "#"
#define MVSH_MAX_LINE   4096

int mvsh_repl()
{
  char line[MVSH_MAX_LINE];
  char *charp;
  while (1) {
    fprintf(stdout, "%s ", MVSH_PROMPT);

    if (fgets(line, MVSH_MAX_LINE, stdin)) {
      if ((charp = strchr(line, '\n')) != NULL)
        *charp = '\0';

      mvsh_command_process(line);
    }
  }

  return 0;
}
