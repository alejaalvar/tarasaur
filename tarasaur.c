/*
 * @file        tarasaur.c
 * @brief       An archiver program
 * @author      Alejandro Alvarado
 * @course      Intro to Operating Systems - CS333-006
 * @date        January 27, 2026
 *
 * @details 
 * This program performs two essential functions: reading
 * files and writing to files. This program is designed to
 * manage a file archive library.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <zlib.h>
#include <stdint.h>
#include "tarasaur.h"

/*
 * Display help info for usage of tarasaur
 *
 * @param program_name - the name of the program from argv
 */
static void usage(const char *program_name) {
    fprintf(stderr, 
            "Usage: %s -[cxtTVf:vh] archive-file file...\n"
            "        -c           create a new archive file\n"
            "        -x           extract members from an existing archive file\n"
            "        -t           short table of contents of archive file\n"
            "        -T           long table of contents of archive file\n"
            "        -V           validate the checksum/hash values\n"
            "        -f filename  name of archive file to use\n"
            "        -v           verbose output\n"
            "        -h           show help text\n",
            program_name);
    return;
}

int
main(int argc, char *argv[])
{
    int opt;
    char *file_name = NULL;
    bool is_verbose = false;

    while ((opt = getopt(argc, argv, "cxtTVf:vh")) != -1)
    {
        switch (opt)
        {
            case 'x':
                break;

            case 'c':
                break;

            case 't':
                break;

            case 'T':
                break;

            case 'V':
                break;

            case 'f':
                file_name = optarg;
                break;

            case 'h':
                usage(argv[0]);
                exit(EXIT_SUCCESS);
                break;

            case 'v':
                is_verbose = true;
                break;

            default:
                fprintf(stderr, "Invalid command line option %c", opt);
                return EXIT_FAILURE;
                break;
        }
    }

    return EXIT_SUCCESS;
}
