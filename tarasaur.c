/*
 * @file        tarasaur.c
 * @brief       An archiver program
 * @author      Alejandro Alvarado
 * @course      Intro to Operating Systems - CS333-006
 * @date        January 28, 2026
 *
 * @details 
 * This program performs two essential functions: reading
 * files and writing to files. This program is designed to
 * manage a file archive library.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <zlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <pwd.h>        // For getpwuid
#include <grp.h>        // For getgrgid
#include <time.h>       // For localtime, strftime
#include <sys/stat.h>   // For mode constants
#include "tarasaur.h"

// Standard page size for efficient I/O operations
#define BUFFER_SIZE 4096
#define MAGIC_BUF_SIZE 128
#define PERMISSIONS_LEN 11
#define TIMESTAMP_LEN 64

/*
 * Display help info for usage of tarasaur
 *
 * @param program_name - the name of the program from argv
 */
static void 
usage(const char *program_name) {
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

/*
 * Converts a mode_t value into a ls-style string (e.g., "-rwxr-xr-x").
 *
 * @param mode - the file mode/permissions
 * @param str - buffer to store the resulting string
 */
static void
perm_to_str(mode_t mode, char *str) {
    // Standard ls-style permission mapping
    str[0] = S_ISDIR(mode) ? 'd' : '-';  // check file type bits

    /*
    By using the bitwise AND operator,
    we can determine which permission is set
    because if the bit is set (str[i]), then
    AND must return true (refer to truth table)
    */

    str[1] = (mode & S_IRUSR) ? 'r' : '-';
    str[2] = (mode & S_IWUSR) ? 'w' : '-';
    str[3] = (mode & S_IXUSR) ? 'x' : '-';
    str[4] = (mode & S_IRGRP) ? 'r' : '-';
    str[5] = (mode & S_IWGRP) ? 'w' : '-';
    str[6] = (mode & S_IXGRP) ? 'x' : '-';
    str[7] = (mode & S_IROTH) ? 'r' : '-';
    str[8] = (mode & S_IWOTH) ? 'w' : '-';
    str[9] = (mode & S_IXOTH) ? 'x' : '-';
    str[10] = '\0';  // this makes our a buffer a cstring
}

/*
 * Converts a struct timespec into a formatted string matching the professor's output.
 * Format: YYYY-MM-DD HH:MM:SS Z (e.g., 1997-02-01 19:10:36 PST).
 *
 * @param ts - pointer to the timespec struct
 * @param str - buffer to store the resulting string
 * @param len - size of the buffer
 */
static void
time_to_str(struct timespec *ts, char *str, size_t len) {
    struct tm *local_tm;
    // Extract the seconds portion for conversion
    local_tm = localtime(&ts->tv_sec);
    if (local_tm) {
        strftime(str, len, "%Y-%m-%d %H:%M:%S %Z", local_tm);
    } else {
        // localtime() has to fail for this code to be run
        snprintf(str, len, "Unknown Time");  // write a fallback into the str
    }
}

/*
 * Process the Table of Contents action (Short and Long).
 * Iterates through the archive to print member information.
 *
 * @param fd - the file descriptor
 * @param member_count - the number of contained files
 * @param archive_name - the archive to be read
 * @param is_verbose - boolean to track verbose flag
 * @param is_long - boolean to track if long listing (-T) is requested
 */
static void 
do_toc(int fd,
       int member_count, 
       const char *archive_name, 
       bool is_verbose,
       bool is_long) {
    
    // 1. SKIP OVER ALL DATA BLOBS FIRST
    // The format is [Size][Data]...[Metadata], so we must jump over data
    for (int i = 0; i < member_count; ++i) {
        /*
        How large (in bytes) is the member file
        being examined on the (i-th) current iteration?
        */
        size_t current_member_size = 0;  // this is used to skip data later

        // Read ONLY the size (8 bytes) of the current member
        if (read(fd, &current_member_size, sizeof(size_t)) != sizeof(size_t)) {
            fprintf(stderr, "Error: Failed to read size for member %d\n", i);
            exit(READ_FAIL);
        }

        // Extra diagnostics
        if (is_verbose) {
            fprintf(stderr, "\tskipping over data for member %d of %ld bytes\n", 
                    i, current_member_size);
        }

        /*
        Our file pointer is now at the beginning of the data
        */

        // Skip the data body to reach the next size field or directory
        if (NULL != archive_name) {
            // It is a real file, we can seek efficiently
            if (lseek(fd, current_member_size, SEEK_CUR) == -1) {
                perror("Lseek failed");
                exit(READ_FAIL);
            }
        } else {
            // It is stdin (pipe), we cannot seek. We must read and discard.
            char junk_buf[BUFFER_SIZE];
            size_t bytes_to_skip = current_member_size;
            while (bytes_to_skip > 0) {
                size_t read_amt = (bytes_to_skip < sizeof(junk_buf)) 
                                    ? bytes_to_skip : sizeof(junk_buf);
                if (read(fd, junk_buf, read_amt) <= 0) {
                    fprintf(stderr, "Error: Unexpected EOF skipping data\n");
                    exit(READ_FAIL);
                }
                bytes_to_skip -= read_amt;
            }
        }
    }

    // 2. PRINT TABLE OF CONTENTS
    printf("Table of contents of tarannosaurus file: \"%s\" with %d members\n",
           archive_name ? archive_name : "stdin", 
           member_count);

    // 3. READ AND PRINT THE METADATA ENTRIES
    // File ptr is now positioned at the beginning of the metadata
    for (int i = 0; i < member_count; ++i) {
        tarasaur_directory_t header;  // this is what we read INTO
        char name_buffer[TARASAUR_MAX_NAME_LEN * 2] = {'\0'};  // any arbitrary size larger than macro
        memset(name_buffer, 0, sizeof(name_buffer));  // ensure buffer is null-terminated

        // Consume the header in one go
        if (read(fd,
                 &header, 
                 sizeof(tarasaur_directory_t)) != sizeof(tarasaur_directory_t)) {
            fprintf(stderr, "Error: Failed to read directory entry %d\n", i);
            exit(READ_FAIL);
        }

        // Safe copy of name to ensure null termination
        strncpy(name_buffer, header.tarasaur_name, TARASAUR_MAX_NAME_LEN);
        printf("\tfile name: %s\n", name_buffer);

        // If Long TOC is requested, print extra details
        if (is_long) {
            char perm_str[PERMISSIONS_LEN];  // read permissions into this string
            char time_str[TIMESTAMP_LEN];  // read timestamp into this
            struct passwd *pwd;
            struct group *grp;

            // Mode
            perm_to_str(header.tarasaur_mode, perm_str);
            printf("\t\tmode: \t\t%s\n", perm_str);

            // User
            pwd = getpwuid(header.tarasaur_uid);
            if (pwd) {
                printf("\t\tuser: \t\t%s\n", pwd->pw_name);
            } else {
                printf("\t\tuser: \t\t%d\n", header.tarasaur_uid);
            }

            // Group
            grp = getgrgid(header.tarasaur_gid);
            if (grp) {
                printf("\t\tgroup: \t\t%s\n", grp->gr_name);
            } else {
                printf("\t\tgroup: \t\t%d\n", header.tarasaur_gid);
            }

            // Size
            printf("\t\tsize: \t\t%ld\n", header.tarasaur_size);

            // Mtime
            time_to_str(&header.tarasaur_mtim, time_str, sizeof(time_str));
            printf("\t\tmtime: \t\t%s\n", time_str);

            // Atime
            time_to_str(&header.tarasaur_atim, time_str, sizeof(time_str));
            printf("\t\tatime: \t\t%s\n", time_str);

            // CRC Headers
            printf("\t\tcrc32 header: \t0x%08x\n", header.crc32_header);
            printf("\t\tcrc32 data: \t0x%08x\n", header.crc32_data);
        }
    }
}

int
main(int argc, char *argv[])
{
    int opt;  // setup for getopt
    char *file_name = NULL;  // this must be NULL for error handling
    bool is_verbose = false;  // this activates extra diagnostics
    tarasaur_action_t action = ACTION_NONE;  // must be default value for error handling

    // Extract the op first - no processing yet
    while ((opt = 
        getopt(argc, argv, "cxtTVf:vh")) != -1)
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

            // This corresponds to ACTION_NONE - the default value
            default:
                fprintf(stderr, 
                        "Invalid command line option %c\n", 
                        opt);
                exit(INVALID_CMD_OPTION);
                break;
        }
    }

    // Now we can proceed with processing
    switch (action)
    {
        case ACTION_CREATE:
            {
                fprintf(stderr, 
                        "Creating...\n");  // placeholder

                /*
                TODO: Implement the file creation logic
                */
            }
            break;
    
        /*
        All of these share the same error handling:
            file open failure,
            read failures,
            incorrect magic number,
            incorrect version number,
        */
        case ACTION_EXTRACT:
        case ACTION_TOC_SHORT:
        case ACTION_TOC_LONG:
        case ACTION_VALIDATE:
            {
                char magic_buf[MAGIC_BUF_SIZE] = {'\0'};  // ensures a null terminator at the end
                int magic_len = strlen(TARASAUR_MAGIC_NUMBER);
                short version;
                int member_count;
                int fd;

                // Can we open the file? Was a file even provided?
                if (file_name) {
                    fd = open(file_name, O_RDONLY);
                    if (-1 == fd) {
                        perror("Error opening archive");
                        exit(EXIT_FAILURE);
                    }
                    // Extra diagnostics
                    if (is_verbose) {
                        fprintf(stderr, 
                                "Reading archive file: \"%s\"\n", 
                                file_name);
                    }
                }
                // No file provided - default to read from STDIN
                else {
                    fd = STDIN_FILENO;
                    fprintf(stderr, 
                            "Reading archive from stdin\n");
                }

                // --- HEADER VALIDATION START ---

                /*
                There are three parts of the header:

                    Magic Number,
                    Version Number,
                    Member Count

                We verify the Magic Number and Version Number
                */
                
                // Verify Magic Number (from the header file)
                if (read(fd, magic_buf, magic_len) != magic_len) {
                     fprintf(stderr,
                             "*** failed to read magic number\n");
                     exit(BAD_MAGIC);
                }
                
                // Wrong file type
                if (strcmp(magic_buf, TARASAUR_MAGIC_NUMBER) != 0) {
                    fprintf(stderr, "Not a tarannosaurus file: \"%s\"\n", 
                            file_name ? file_name : "stdin");
                    exit(BAD_MAGIC);
                }

                // Verify Version (from the header file)
                if (read(fd, &version, sizeof(short)) != sizeof(short)) {
                    fprintf(stderr, "Error: Failed to read version\n");
                    exit(BAD_MAGIC);
                }
                
                if (version != TARASAUR_VERSION) {
                    fprintf(stderr, "Error: Bad version number %d\n", version);
                    exit(BAD_MAGIC); 
                }

                // 3. Get Member Count
                if (read(fd, &member_count, sizeof(int)) != sizeof(int)) {
                    fprintf(stderr, "Error: Failed to read member count\n");
                    exit(READ_FAIL);
                }
                
                // --- HEADER VALIDATION END ---

                // Dispatch to specific handlers
                switch (action) {
                    case ACTION_TOC_SHORT:
                        do_toc(fd, member_count, file_name, is_verbose, false);
                        break;
                    
                    case ACTION_TOC_LONG:
                        do_toc(fd, member_count, file_name, is_verbose, true);
                        break;
                    
                    case ACTION_EXTRACT:
                        // do_extract(fd, member_count, file_name, is_verbose);
                        break;
                    
                    case ACTION_VALIDATE:
                        // do_validate(fd, member_count, file_name, is_verbose);
                        break;
                        
                    default: 
                        break;
                }

                if (file_name) close(fd);
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