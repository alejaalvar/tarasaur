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

int
main(int argc, char *argv[])
{
    int opt;

    while ((opt = getopt(argc, argv, "xctTVfhv")) != -1)
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
                break;

            case 'h':
                printf("Some help text\n");
                break;

            case 'v':
                printf("Some verbose text\n");
                break;

            default:
                fprintf(stderr, "Invalid command line option %c", opt);
                return EXIT_FAILURE;
                break;
        }
    }


    return EXIT_SUCCESS;
}
