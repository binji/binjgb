/*
 * Copyright (C) 2016 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdlib.h>

struct OptionParser;

typedef struct Option {
  char short_name;
  const char* long_name;
  int has_value;
} Option;

typedef enum OptionResultKind {
  OPTION_RESULT_KIND_UNKNOWN,          /* An unspecified option. */
  OPTION_RESULT_KIND_EXPECTED_VALUE,   /* Expected an option arg: -j 3 */
  OPTION_RESULT_KIND_BAD_SHORT_OPTION, /* Short option too long.: -foo */
  OPTION_RESULT_KIND_OPTION,           /* -h, --help, etc. */
  OPTION_RESULT_KIND_ARG,              /* foo.txt, etc. */
  OPTION_RESULT_KIND_DONE,             /* When there are no args left. */
} OptionResultKind;

typedef struct OptionResult {
  OptionResultKind kind;
  const Option* option;
  union {
    char* value; /* if KIND_OPTION: the value of the option, if any. */
    char* arg;   /* if KIND_ARG: the bare arg. */
  };
} OptionResult;

#ifdef __cplusplus
extern "C" {
#endif

struct OptionParser* option_parser_new(const Option* options,
                                       size_t num_options, int argc,
                                       char** argv);
OptionResult option_parser_next(struct OptionParser*);
void option_parser_delete(struct OptionParser*);

#ifdef __cplusplus
} /* extern "C" */
#endif
