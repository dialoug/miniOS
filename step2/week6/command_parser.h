#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#include <stddef.h>

enum { MAX_ARGUMENTS = 8 };

int split_command(char *line,
                  char *arguments[],
                  size_t argument_capacity,
                  size_t *argument_count);

#endif
