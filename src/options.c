/*
 * Copyright (C) 2016 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "options.h"

#include <string.h>

typedef struct OptionParser {
  const Option* options;
  size_t num_options;
  int argc;
  char** argv;
  int arg_index;
} OptionParser;

OptionParser* option_parser_new(const Option* options, size_t num_options,
                                int argc, char** argv) {
  OptionParser* parser = calloc(1, sizeof(OptionParser));
  parser->options = options;
  parser->num_options = num_options;
  parser->argc = argc;
  parser->argv = argv;
  parser->arg_index = 1; /* Skip the program name (arg 0). */
  return parser;
}

static OptionResult make_option_result(OptionResultKind kind,
                                       const Option* option, char* value) {
  OptionResult result;
  result.kind = kind;
  result.option = option;
  result.value = value;
  return result;
}

static OptionResult make_option_result_with_value(OptionResultKind kind,
                                                  const Option* option,
                                                  char* value) {
  if (strlen(value) == 0) {
    return make_option_result(OPTION_RESULT_KIND_EXPECTED_VALUE, option, NULL);
  } else {
    return make_option_result(OPTION_RESULT_KIND_OPTION, option, value);
  }
}

OptionResult option_parser_next(OptionParser* parser) {
  if (parser->arg_index >= parser->argc) {
    return make_option_result(OPTION_RESULT_KIND_DONE, NULL, NULL);
  }

  char* arg = parser->argv[parser->arg_index++];
  if (arg[0] == '-') {
    size_t i;
    if (arg[1] == '-') {
      /* Long option. */
      for (i = 0; i < parser->num_options; ++i) {
        const Option* option = &parser->options[i];
        if (!option->long_name) {
          continue;
        }

        size_t long_name_len = strlen(option->long_name);
        if (strncmp(&arg[2], option->long_name, long_name_len) == 0) {
          if (option->has_value) {
            if (arg[2 + long_name_len] == '=') {
              char* value = &arg[2 + long_name_len + 1];
              return make_option_result_with_value(OPTION_RESULT_KIND_OPTION,
                                                   option, value);
            } else {
              if (parser->arg_index >= parser->argc) {
                return make_option_result(OPTION_RESULT_KIND_EXPECTED_VALUE,
                                          option, NULL);
              }

              char* value = parser->argv[parser->arg_index++];
              return make_option_result_with_value(OPTION_RESULT_KIND_OPTION,
                                                   option, value);
            }
          } else {
            return make_option_result(OPTION_RESULT_KIND_OPTION, option, NULL);
          }
        }
      }
    } else {
      /* Short option. */
      for (i = 0; i < parser->num_options; ++i) {
        const Option* option = &parser->options[i];
        if (!option->short_name) {
          continue;
        }

        if (option->short_name == arg[1]) {
          if (arg[2] == 0) {
            if (option->has_value) {
              if (parser->arg_index >= parser->argc) {
                return make_option_result(OPTION_RESULT_KIND_EXPECTED_VALUE,
                                          option, NULL);
              }

              char* value = parser->argv[parser->arg_index++];
              return make_option_result_with_value(OPTION_RESULT_KIND_OPTION,
                                                   option, value);
            } else {
              return make_option_result(OPTION_RESULT_KIND_OPTION, option,
                                        NULL);
            }
          } else {
            return make_option_result(OPTION_RESULT_KIND_BAD_SHORT_OPTION,
                                      option, arg);
          }
        }
      }
    }

    return make_option_result(OPTION_RESULT_KIND_UNKNOWN, NULL, arg);
  } else {
    /* Argument. */
    return make_option_result(OPTION_RESULT_KIND_ARG, NULL, arg);
  }
}

void option_parser_delete(OptionParser* parser) {
  free(parser);
}
