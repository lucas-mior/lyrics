#include "cli.h"

#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char **argv) {
    CliOptions options;
    int parse_result;

    cli_options_init(&options);
    parse_result = cli_parse(&options, argc, argv);
    if (parse_result > 0) {
        exit(EXIT_SUCCESS);
    }
    if (parse_result < 0) {
        cli_print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    cli_print_options(&options);
    fprintf(stderr, "audio extraction is not implemented yet\n");

    exit(EXIT_SUCCESS);
}
