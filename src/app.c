#include "app.h"

#include "cli.h"

#include <stdio.h>
#include <stdlib.h>

int32
app_run(int argc, char **argv) {
    CliOptions options;
    int32 parse_result;

    cli_options_init(&options);
    parse_result = cli_parse(&options, argc, argv);
    if (parse_result > 0) {
        return EXIT_SUCCESS;
    }
    if (parse_result < 0) {
        cli_print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    cli_print_options(&options);
    fprintf(stderr, "audio extraction is not implemented yet\n");

    return EXIT_SUCCESS;
}

#if TESTING_app

int
main(void) {
    return 0;
}

#endif /* TESTING_app */
