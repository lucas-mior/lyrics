#if !defined(TESTING_app)
#define TESTING_app 0
#endif

#define CBASE_API_DECL static
#define CBASE_API_DEF static
#define CBASE_IMPLEMENT
#include "cbase.h"

int32
main(int32 argc, char **argv) {
    program = argv[0];

    if ((argc == 2)
        && (strequal(argv[1], "-h") || strequal(argv[1], "--help"))) {
        error2("usage: %s <voice-audio> <lyrics.txt> <output.lrc>\n",
               program);
        exit(EXIT_SUCCESS);
    }

    if (argc != 4) {
        error2("usage: %s <voice-audio> <lyrics.txt> <output.lrc>\n",
               program);
        exit(EXIT_FAILURE);
    }

    error2("%s is not implemented yet\n", program);
    error2("voice audio: %s\n", argv[1]);
    error2("lyrics text: %s\n", argv[2]);
    error2("output lrc:  %s\n", argv[3]);

    exit(EXIT_FAILURE);
}
