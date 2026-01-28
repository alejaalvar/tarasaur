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
    tarasaur_action_t action = ACTION_NONE;

    while ((opt = getopt(argc, argv, "cxtTVf:vh")) != -1)
    {
        switch (opt)
        {
            case 'x':
                action = ACTION_EXTRACT;
                break;

            case 'c':
                action = ACTION_CREATE;
                break;

            case 't':
                /*
                Example output for the short table of contents without verbose:

                    Reading archive file: "test-archive.tarasaur"
                    Table of contents of tarannosaurus file: "test-archive.tarasaur" with 3 members
                            file name: 0-s.txt
                            file name: 1-s.txt
                            file name: 2-s.txt

                Example output for the short able of contents with verbose:

                    Reading archive file: "test-archive.tarasaur"
                            skipping over data for member 0 of 0 bytes
                            skipping over data for member 1 of 41 bytes
                            skipping over data for member 2 of 82 bytes
                    Table of contents of tarannosaurus file: "test-archive.tarasaur" with 3 members
                            file name: 0-s.txt
                            file name: 1-s.txt
                            file name: 2-s.txt

                */
                action = ACTION_TOC_SHORT;
                break;

            case 'T':
                action = ACTION_TOC_LONG;
                break;

            case 'V':
                action = ACTION_VALIDATE;
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
                fprintf(stderr, "Invalid command line option %c\n", opt);
                exit(INVALID_CMD_OPTION);
                break;
        }
    }

    /*
    This block of code silences compiler
    warnings during testing of each piece
    of functionality before the is_verbose
    and file_name variables are being used.
    It will be removed when the functionality
    requring those vars is implemented
    */
    {
        fprintf(stderr, 
                "Verbose: %d\n"
                "Action: %d\n"
                "File name: %s\n",
                is_verbose, action, file_name);
    }

    // if (ACTION_NONE == action) {
    //     fprintf(stderr, "*** %s No action specified\n", 
    //             argv[0]);
    //     exit(NO_ACTION_GIVEN);
    // }

    switch (action)
    {
        case ACTION_CREATE:
            {
                fprintf(stderr, "Creating...\n");
            }
            break;
    
        case ACTION_EXTRACT:
            {
                fprintf(stderr, "Extracting...\n");
            }
            break;

        case ACTION_TOC_SHORT:
            {
                fprintf(stderr, "Short TOC...\n");
            }
            break;

        case ACTION_TOC_LONG:
            {
                fprintf(stderr, "Long TOC...\n");
            }
            break;

        case ACTION_VALIDATE:
            {
                fprintf(stderr, "Validating...\n");
            }
            break;

        default:
            fprintf(stderr, "*** %s No action specified\n", 
                    argv[0]);
            exit(NO_ACTION_GIVEN);
            break;
    }

    return EXIT_SUCCESS;
}
