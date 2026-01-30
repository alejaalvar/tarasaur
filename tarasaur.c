/*
 * @file        tarasaur.c
 * @brief       An archiver program
 * @author      Alejandro Alvarado
 * @course      Intro to Operating Systems - CS333-006
 * @date        January 30, 2026
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
#define NUM_TIMESTAMPS 2

// Valid command line options
#define OPTIONS "cxtTVf:vh"

/*
 * Read from stdin
 *
 * @param fd - the file descriptor
 * @param current_member_size - the number of archived files
 */
 static void 
 read_stdin(int fd, size_t current_member_size) {
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

/*
 * Display help info for usage of tarasaur
 *
 * @param program_name - the name of the program from argv
 */
static void 
usage(const char *program_name) {
    fprintf(stderr, 
            "Usage: %s -[%s] archive-file file...\n"
            "        -c           create a new archive file\n"
            "        -x           extract members from an existing archive file\n"
            "        -t           short table of contents of archive file\n"
            "        -T           long table of contents of archive file\n"
            "        -V           validate the checksum/hash values\n"
            "        -f filename  name of archive file to use\n"
            "        -v           verbose output\n"
            "        -h           show help text\n",
            program_name, OPTIONS);
    return;
}

/*
 * Shorthand help message for usage of tarasaur.
 * Used when the action flag is still ACTION_NONE.
 *
 * @param program_name - the name of the program from argv
 */
static void 
short_usage(const char *program_name) {
   fprintf(stderr, 
           "Usage: %s %s\n", 
           program_name, OPTIONS);
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

// Forward declarations for main worker functions
static void do_toc(int fd, int member_count, const char *archive_name,
                   bool is_verbose, bool is_long);
static int do_validate(int fd, int member_count, const char *archive_name,
                       bool is_verbose);
static void do_extract(int fd, int member_count, const char *archive_name,
                       bool is_verbose);
static void do_create(const char *archive_name, char **file_list,
                      int num_files, bool is_verbose);

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
            read_stdin(fd, current_member_size);
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

/*
 * Calculate CRC32 checksum for a block of data using zlib.
 *
 * @param data - pointer to the data buffer
 * @param size - size of the data in bytes
 * @return The calculated CRC32 checksum
 */
static uint32_t
get_crc(const void *data, size_t size) {
    uint32_t crc = crc32(0L, Z_NULL, 0);  // Initialize zlib CRC state
    crc = crc32(crc, (const Bytef *)data, size);  // Calculate CRC
    return crc;
}

/*
 * Validate CRC32 checksums for all archive members.
 * Checks both header and data CRCs for each member.
 *
 * @param fd - the file descriptor
 * @param member_count - the number of contained files
 * @param archive_name - the archive to be validated (NULL if stdin)
 * @param is_verbose - boolean to track verbose flag
 * @return EXIT_SUCCESS if all validations pass, VALIDATE_ERROR otherwise
 */
static int
do_validate(int fd,
            int member_count,
            const char *archive_name,
            bool is_verbose) {

    tarasaur_directory_t *headers = NULL;
    size_t *data_sizes = NULL;
    int validation_errors = 0;

    // Allocate arrays for metadata and data sizes
    headers = calloc(member_count, sizeof(tarasaur_directory_t));
    if (!headers) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    data_sizes = calloc(member_count, sizeof(size_t));
    if (!data_sizes) {
        perror("calloc");
        free(headers);
        exit(EXIT_FAILURE);
    }

    // Print initial message to stderr
    if (archive_name) {
        fprintf(stderr, "Validating archive file: \"%s\"\n", archive_name);
    } else {
        fprintf(stderr, "Validating archive from stdin\n");
    }

    // Step 1: Skip over all data sections while collecting sizes
    for (int i = 0; i < member_count; ++i) {
        // Read size of this member's data
        if (read(fd, &data_sizes[i], sizeof(size_t)) != sizeof(size_t)) {
            fprintf(stderr, "Error: Failed to read size for member %d\n", i);
            free(headers);
            free(data_sizes);
            exit(READ_FAIL);
        }

        // Verbose output
        if (is_verbose) {
            fprintf(stderr, "\tChecking data for member %d of %zd bytes\n",
                    i, data_sizes[i]);
        }

        // Skip past the data blob
        if (archive_name) {
            // Regular file - can seek
            if (lseek(fd, data_sizes[i], SEEK_CUR) == -1) {
                perror("lseek");
                free(headers);
                free(data_sizes);
                exit(READ_FAIL);
            }
        } else {
            // Pipe/stdin - must read and discard
            read_stdin(fd, data_sizes[i]);
        }
    }

    // Step 2: Read all metadata structures
    for (int i = 0; i < member_count; ++i) {
        if (read(fd, &headers[i], sizeof(tarasaur_directory_t))
            != sizeof(tarasaur_directory_t)) {
            fprintf(stderr, "Error: Failed to read directory entry %d\n", i);
            free(headers);
            free(data_sizes);
            exit(READ_FAIL);
        }
    }

    // Step 3: Validate each member
    for (int i = 0; i < member_count; ++i) {
        void *data_buffer = NULL;
        uint32_t calculated_data_crc;
        uint32_t calculated_header_crc;
        uint32_t stored_data_crc;
        uint32_t stored_header_crc;
        int data_valid;
        int header_valid;
        tarasaur_directory_t header_copy;

        // --- Validate Data CRC ---

        // Seek to data location using tarasaur_data_offset
        if (lseek(fd, headers[i].tarasaur_data_offset, SEEK_SET) == -1) {
            perror("lseek");
            free(headers);
            free(data_sizes);
            exit(READ_FAIL);
        }

        // Allocate buffer for data
        data_buffer = malloc(data_sizes[i]);
        if (!data_buffer) {
            perror("malloc");
            free(headers);
            free(data_sizes);
            exit(EXIT_FAILURE);
        }

        // Read the data
        if (read(fd, data_buffer, data_sizes[i]) != (ssize_t)data_sizes[i]) {
            fprintf(stderr, "Error: Failed to read data for member %d\n", i);
            free(data_buffer);
            free(headers);
            free(data_sizes);
            exit(READ_FAIL);
        }

        // Calculate data CRC
        calculated_data_crc = get_crc(data_buffer, data_sizes[i]);
        stored_data_crc = headers[i].crc32_data;
        data_valid = (calculated_data_crc == stored_data_crc);

        // --- Validate Header CRC ---

        // Make a copy of the header
        header_copy = headers[i];

        // CRITICAL: Zero out the CRC fields before calculating
        header_copy.crc32_data = 0;
        header_copy.crc32_header = 0;

        // Calculate header CRC
        calculated_header_crc = get_crc(&header_copy, sizeof(tarasaur_directory_t));
        stored_header_crc = headers[i].crc32_header;
        header_valid = (calculated_header_crc == stored_header_crc);

        // --- Print Results ---

        // Print member name (padded to 25 chars)
        fprintf(stdout, "Archive member: %-25s\n", headers[i].tarasaur_name);

        // Print header validation result
        fprintf(stdout, "\theader: file 0x%08x      calculated 0x%08x  %s\n",
                stored_header_crc, calculated_header_crc,
                header_valid ? "***   valid ***" : "*** INVALID ***");

        // Print data validation result
        fprintf(stdout, "\tdata:   file 0x%08x      calculated 0x%08x  %s\n",
                stored_data_crc, calculated_data_crc,
                data_valid ? "***   valid ***" : "*** INVALID ***");

        // Track errors
        if (!header_valid || !data_valid) {
            ++validation_errors;
        }

        // Clean up data buffer
        free(data_buffer);
    }

    // Cleanup
    free(headers);
    free(data_sizes);

    // Close file if not stdin
    if (archive_name) {
        close(fd);
    }

    // Return appropriate exit code
    if (validation_errors > 0) {
        return VALIDATE_ERROR;
    } else {
        return EXIT_SUCCESS;
    }
}

/*
 * Open an archive and read/validate its header.
 * Returns the file descriptor and member count via out parameters.
 *
 * @param archive_name - the archive filename (NULL for stdin)
 * @param is_verbose - boolean to track verbose flag
 * @param fd_out - output parameter for file descriptor
 * @param member_count_out - output parameter for member count
 */
static void
open_and_read_archive_header(const char *archive_name,
                              bool is_verbose,
                              int *fd_out,
                              int *member_count_out) {
    char magic_buf[MAGIC_BUF_SIZE] = {'\0'};
    int magic_len = strlen(TARASAUR_MAGIC_NUMBER);
    short version;
    int member_count;
    int fd;

    // Open archive file or use stdin
    if (archive_name) {
        fd = open(archive_name, O_RDONLY);
        if (-1 == fd) {
            perror("Error opening archive");
            exit(EXIT_FAILURE);
        }
        // Print message for all operations
        fprintf(stderr,
                "Reading archive file: \"%s\"\n",
                archive_name);
    } else {
        fd = STDIN_FILENO;
        fprintf(stderr,
                "Reading archive from stdin\n");
    }

    // Verify Magic Number
    if (read(fd, magic_buf, magic_len) != magic_len) {
        fprintf(stderr,
                "*** failed to read magic number\n");
        exit(BAD_MAGIC);
    }

    if (strcmp(magic_buf, TARASAUR_MAGIC_NUMBER) != 0) {
        fprintf(stderr, "Not a tarannosaurus file: \"%s\"\n",
                archive_name ? archive_name : "stdin");
        exit(BAD_MAGIC);
    }

    // Verify Version
    if (read(fd, &version, sizeof(short)) != sizeof(short)) {
        fprintf(stderr, "Error: Failed to read version\n");
        exit(BAD_MAGIC);
    }

    if (version != TARASAUR_VERSION) {
        fprintf(stderr, "Error: Bad version number %d\n", version);
        exit(BAD_MAGIC);
    }

    // Get Member Count
    if (read(fd, &member_count, sizeof(int)) != sizeof(int)) {
        fprintf(stderr, "Error: Failed to read member count\n");
        exit(READ_FAIL);
    }

    // Return values via out parameters
    *fd_out = fd;
    *member_count_out = member_count;

    (void)is_verbose;  // Suppress unused parameter warning if needed
}

/*
 * Extract files from an archive and restore their metadata.
 *
 * @param fd - the file descriptor
 * @param member_count - the number of contained files
 * @param archive_name - the archive to extract from (NULL if stdin)
 * @param is_verbose - boolean to track verbose flag
 */
static void
do_extract(int fd,
           int member_count,
           const char *archive_name,
           bool is_verbose) {

    tarasaur_directory_t *headers = NULL;  // this is how we store metadata
    size_t *data_sizes = NULL;  // how we store size of each files data

    // Allocate arrays for metadata and data sizes
    headers = calloc(member_count, sizeof(tarasaur_directory_t));
    if (!headers) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    data_sizes = calloc(member_count, sizeof(size_t));
    if (!data_sizes) {
        perror("calloc");
        free(headers);
        exit(EXIT_FAILURE);
    }

    // Step 1: Skip over all data sections while collecting sizes
    for (int i = 0; i < member_count; ++i) {  // iterate through each file
        // Read size of this member's data
        if (read(fd, &data_sizes[i], sizeof(size_t)) != sizeof(size_t)) {
            fprintf(stderr, "Error: Failed to read size for member %d\n", i);
            free(headers);
            free(data_sizes);
            exit(READ_FAIL);
        }

        // Skip past the data blob
        if (archive_name) {
            // Regular file - can seek
            if (lseek(fd, data_sizes[i], SEEK_CUR) == -1) {
                perror("lseek");
                free(headers);
                free(data_sizes);
                exit(READ_FAIL);
            }
        } else {
            // Pipe/stdin - must read and discard
            read_stdin(fd, data_sizes[i]);
        }
    }

    // Step 2: Read all metadata structures
    for (int i = 0; i < member_count; ++i) {
        if (read(fd, &headers[i], sizeof(tarasaur_directory_t))
            != sizeof(tarasaur_directory_t)) {
            fprintf(stderr, "Error: Failed to read directory entry %d\n", i);
            free(headers);
            free(data_sizes);
            exit(READ_FAIL);
        }
    }

    // Step 3: Extract each member
    for (int i = 0; i < member_count; ++i) {
        char buffer[BUFFER_SIZE];
        int out_fd;
        size_t remaining;
        struct timespec times[NUM_TIMESTAMPS];  // store our timestamps

        // Verbose output to stderr
        if (is_verbose) {
            fprintf(stderr, "\tExtracting member data: %s   size: %10zd\n",
                    headers[i].tarasaur_name, data_sizes[i]);
        }

        // Create output file
        out_fd = open(headers[i].tarasaur_name,
                      O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (out_fd == -1) {
            perror("open");
            free(headers);
            free(data_sizes);
            exit(EXTRACT_FAIL);
        }

        // Seek to data location using tarasaur_data_offset
        if (lseek(fd, headers[i].tarasaur_data_offset, SEEK_SET) == -1) {
            perror("lseek");
            close(out_fd);
            free(headers);
            free(data_sizes);
            exit(READ_FAIL);
        }

        // Read and write data in chunks
        remaining = data_sizes[i];
        while (remaining > 0) {
            size_t to_read = MIN(remaining, sizeof(buffer));
            ssize_t bytes_read = read(fd, buffer, to_read);

            if (bytes_read <= 0) {
                fprintf(stderr, "Error: Failed to read data for member %d\n", i);
                close(out_fd);
                free(headers);
                free(data_sizes);
                exit(READ_FAIL);
            }

            if (write(out_fd, buffer, bytes_read) != bytes_read) {
                perror("write");
                close(out_fd);
                free(headers);
                free(data_sizes);
                exit(EXTRACT_FAIL);
            }

            remaining -= bytes_read;
        }

        // Close output file
        close(out_fd);

        // Restore permissions as they were originally stored
        if (chmod(headers[i].tarasaur_name, 
                  headers[i].tarasaur_mode) == -1) {
            perror("chmod");
            // Continue extraction even if chmod fails
        }

        // Restore timestamps
        times[0] = headers[i].tarasaur_atim;  // Access time
        times[1] = headers[i].tarasaur_mtim;  // Modification time
        /*
        Set file timestamps with precision
        Use current dir
        times - the array with our timestamps
        0 - no special flags
        */
        if (utimensat(AT_FDCWD, headers[i].tarasaur_name, times, 0) == -1) {
            perror("utimensat");
            // Continue extraction even if utimensat fails
        }
    }

    // Print extraction summary to stdout
    printf("Extracting contents of tarannosaurus file: \"%s\" with %d members\n",
           archive_name ? archive_name : "stdin",
           member_count);

    // Cleanup
    free(headers);
    free(data_sizes);

    // Close archive if not stdin
    if (archive_name) {
        close(fd);
    }
}

/*
 * Parse command line options and return the selected action.
 *
 * @param argc - argument count from main
 * @param argv - argument vector from main
 * @param file_name_out - output parameter for archive filename
 * @param is_verbose_out - output parameter for verbose flag
 * @return The selected action (ACTION_CREATE, ACTION_EXTRACT, etc.)
 */
static tarasaur_action_t
parse_command_line_options(int argc,
                           char *argv[],
                           char **file_name_out,
                           bool *is_verbose_out) {
    int opt;
    tarasaur_action_t action = ACTION_NONE;
    char *file_name = NULL;
    bool is_verbose = false;

    while ((opt = getopt(argc, argv, OPTIONS)) != -1) {
        switch (opt) {
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

            default:
                short_usage(argv[0]);
                exit(INVALID_CMD_OPTION);
                break;
        }
    }

    *file_name_out = file_name;
    *is_verbose_out = is_verbose;
    return action;
}

/*
 * Handle archive reading operations (extract, TOC, validate).
 * Opens archive, validates header, and dispatches to appropriate handler.
 *
 * @param action - the specific action to perform
 * @param file_name - the archive filename (NULL for stdin)
 * @param is_verbose - verbose flag
 */
static void
handle_archive_reading_action(tarasaur_action_t action,
                               char *file_name,
                               bool is_verbose) {
    int fd;
    int member_count;

    // Open archive and read/validate header
    open_and_read_archive_header(file_name, is_verbose, &fd, &member_count);

    // Dispatch to specific handlers
    switch (action) {
        case ACTION_TOC_SHORT:
            do_toc(fd, member_count, file_name, is_verbose, false);
            break;

        case ACTION_TOC_LONG:
            do_toc(fd, member_count, file_name, is_verbose, true);
            break;

        case ACTION_EXTRACT:
            do_extract(fd, member_count, file_name, is_verbose);
            break;

        case ACTION_VALIDATE:
            exit(do_validate(fd, member_count, file_name, is_verbose));
            break;

        default:
            break;
    }

    if (file_name) close(fd);
}

/*
 * Handle archive creation.
 *
 * @param file_name - the archive filename to create
 * @param file_list - array of files to add to archive
 * @param num_files - number of files in file_list
 * @param is_verbose - verbose flag
 */
static void
handle_create_action(char *file_name,
                     char **file_list,
                     int num_files,
                     bool is_verbose) {
    do_create(file_name, file_list, num_files, is_verbose);
}

/*
 * Create a new archive file from a list of input files.
 *
 * @param archive_name - the archive file to create
 * @param file_list - array of filenames to add to archive
 * @param num_files - number of files in file_list
 * @param is_verbose - boolean to track verbose flag
 */
static void
do_create(const char *archive_name,
          char **file_list,
          int num_files,
          bool is_verbose) {

    tarasaur_directory_t *headers = NULL;  // temp holding space so we can write TO the archive
    size_t *file_sizes = NULL;  // track every input file's size
    int archive_fd;
    off_t current_offset;  // we will write this offset into the archive
    short version;
    struct stat st;  // the buffer we read into from fstat
    int file_fd;  // this is where we extract our data blobs from
    char buffer[BUFFER_SIZE];  // read data from file into this buffer
    size_t remaining;  // used to calculate the correct num of bytes to read
    void *file_data;
    size_t bytes_read_total;
    tarasaur_directory_t header_copy;  // necessary so we can compare, 
                                       // and void circular dependency

    // Validate inputs
    if (!archive_name) {
        fprintf(stderr, "Error: No archive filename specified\n");
        exit(NO_ARCHIVE_NAME);
    }

    if (num_files == 0) {
        fprintf(stderr, "Error: No files specified to archive\n");
        exit(CREATE_FAIL);
    }

    // Allocate arrays for headers and file sizes
    headers = calloc(num_files, sizeof(tarasaur_directory_t));
    if (!headers) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    file_sizes = calloc(num_files, sizeof(size_t));
    if (!file_sizes) {
        perror("calloc");
        free(headers);
        exit(EXIT_FAILURE);
    }

    // Pass 1: Collect file metadata and calculate offsets

    /*
    Visualize the structure of the archive:
        length of magic num + sizeof(version_number) + sizeof(num_files)
        strlen # bytes + 2 bytes + 4 bytes
    */
    current_offset = strlen(TARASAUR_MAGIC_NUMBER) + sizeof(short) + sizeof(int);

    for (int i = 0; i < num_files; ++i) {
        // Open input file
        file_fd = open(file_list[i], O_RDONLY);
        if (file_fd == -1) {
            perror(file_list[i]);
            free(headers);
            free(file_sizes);
            exit(CREATE_FAIL);
        }

        // Get file metadata
        if (fstat(file_fd, &st) == -1) {
            perror("fstat");
            close(file_fd);
            free(headers);
            free(file_sizes);
            exit(CREATE_FAIL);
        }

        close(file_fd);

        // Check if it's a regular file
        if (!S_ISREG(st.st_mode)) {
            fprintf(stderr, "Error: %s is not a regular file\n", file_list[i]);
            free(headers);
            free(file_sizes);
            exit(CREATE_FAIL);
        }

        // Store file size
        file_sizes[i] = st.st_size;  // used to calculate the offset

        // Calculate data offset for this file

        /*
        Remember that after the int (4 bytes) that stores the
        number of members, there is a size_t (8 bytes) that stores
        the number of bytes of member data
        */
        headers[i].tarasaur_data_offset = current_offset + sizeof(size_t);
        current_offset = headers[i].tarasaur_data_offset + st.st_size;  // we now have the offset value

        // Store filename (truncate if necessary)
        strncpy(headers[i].tarasaur_name, file_list[i], TARASAUR_MAX_NAME_LEN - 1);
        headers[i].tarasaur_name[TARASAUR_MAX_NAME_LEN - 1] = '\0';  // make sure it is a cstring

        // Store metadata
        headers[i].tarasaur_size = st.st_size;
        headers[i].tarasaur_mode = st.st_mode;
        headers[i].tarasaur_uid = st.st_uid;
        headers[i].tarasaur_gid = st.st_gid;
        headers[i].tarasaur_atim = st.st_atim;
        headers[i].tarasaur_mtim = st.st_mtim;

        // CRCs will be calculated during write pass
        headers[i].crc32_data = 0;
        headers[i].crc32_header = 0;
    }

    // Open archive file for writing
    archive_fd = open(archive_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (archive_fd == -1) {
        perror(archive_name);
        free(headers);
        free(file_sizes);
        exit(CREATE_FAIL);
    }

    if (is_verbose) {
        fprintf(stderr, "Creating archive file: \"%s\"\n", archive_name);
    }

    // Write header: magic number, version, member count
    if (write(archive_fd, TARASAUR_MAGIC_NUMBER, strlen(TARASAUR_MAGIC_NUMBER))
        != (ssize_t)strlen(TARASAUR_MAGIC_NUMBER)) {
        perror("write");
        close(archive_fd);
        free(headers);
        free(file_sizes);
        exit(CREATE_FAIL);
    }

    version = TARASAUR_VERSION;
    if (write(archive_fd, &version, sizeof(short)) != sizeof(short)) {
        perror("write");
        close(archive_fd);
        free(headers);
        free(file_sizes);
        exit(CREATE_FAIL);
    }

    if (write(archive_fd, &num_files, sizeof(int)) != sizeof(int)) {
        perror("write");
        close(archive_fd);
        free(headers);
        free(file_sizes);
        exit(CREATE_FAIL);
    }

    // Pass 2: Write data sections and calculate data CRCs
    for (int i = 0; i < num_files; ++i) {
        file_data = NULL;
        bytes_read_total = 0;

        if (is_verbose) {
            fprintf(stderr, "\tAdding member %d: \"%s\"   size: %10zd\n",
                    i, file_list[i], file_sizes[i]);
        }

        // Write size
        if (write(archive_fd, &file_sizes[i], sizeof(size_t)) != sizeof(size_t)) {
            perror("write");
            close(archive_fd);
            free(headers);
            free(file_sizes);
            exit(CREATE_FAIL);
        }

        // Open input file
        file_fd = open(file_list[i], O_RDONLY);
        if (file_fd == -1) {
            perror(file_list[i]);
            close(archive_fd);
            free(headers);
            free(file_sizes);
            exit(CREATE_FAIL);
        }

        // Allocate buffer for entire file to calculate CRC
        if (file_sizes[i] > 0) {
            file_data = malloc(file_sizes[i]);
            if (!file_data) {
                perror("malloc");
                close(file_fd);
                close(archive_fd);
                free(headers);
                free(file_sizes);
                exit(EXIT_FAILURE);
            }
        }

        // Read and write file data
        remaining = file_sizes[i];
        while (remaining > 0) {
            /*
            We use MIN to read the correct amount of bytes
            */
            size_t to_read = MIN(remaining, sizeof(buffer));
            ssize_t bytes_read = read(file_fd, buffer, to_read);

            if (bytes_read <= 0) {
                fprintf(stderr, "Error: Failed to read from %s\n", file_list[i]);
                free(file_data);
                close(file_fd);
                close(archive_fd);
                free(headers);
                free(file_sizes);
                exit(READ_FAIL);
            }

            // Copy to file_data buffer for CRC calculation
            if (file_data) {
                /*
                Pointer arithmetic to update the destination we
                read to based on our iteration. We "move forward"
                by the amount of bytes_read_total

                Aka, give me the address that is bytes_read_total
                bytes after the start of file_data
                */
                memcpy((char *)file_data + bytes_read_total, 
                       buffer, 
                       bytes_read);
                bytes_read_total += bytes_read;
            }

            // Write to archive
            if (write(archive_fd, buffer, bytes_read) != bytes_read) {
                perror("write");
                free(file_data);
                close(file_fd);
                close(archive_fd);
                free(headers);
                free(file_sizes);
                exit(CREATE_FAIL);
            }

            remaining -= bytes_read;
        }

        close(file_fd);

        // Calculate data CRC
        if (file_sizes[i] > 0) {  // does the file have content?
            headers[i].crc32_data = get_crc(file_data, file_sizes[i]);
            free(file_data);
        } else {
            headers[i].crc32_data = get_crc(NULL, 0);
        }
    }

    // Pass 3: Write metadata section with header CRCs
    for (int i = 0; i < num_files; ++i) {
        // Calculate header CRC (must zero out CRC fields first)

        /*
        We use a header copy because we need to zero out
        the crc fields, but in order to calculate the header
        crc value, we need the crc value we just calculated.

        The only way around this is to use a temporary so we
        do not lose the data crc value we just computed
        */
        header_copy = headers[i];
        header_copy.crc32_data = 0;
        header_copy.crc32_header = 0;
        headers[i].crc32_header = get_crc(&header_copy, 
                                          sizeof(tarasaur_directory_t));

        // Write header
        if (write(archive_fd, &headers[i], sizeof(tarasaur_directory_t))
            != sizeof(tarasaur_directory_t)) {
            perror("write");
            close(archive_fd);
            free(headers);
            free(file_sizes);
            exit(CREATE_FAIL);
        }
    }

    // Print summary
    printf("Created archive file: \"%s\" with %d members\n", 
            archive_name, num_files);

    // Cleanup
    close(archive_fd);
    free(headers);
    free(file_sizes);
}

int
main(int argc, char *argv[])
{
    char *file_name = NULL;
    bool is_verbose = false;
    tarasaur_action_t action;

    // Parse command line options
    action = parse_command_line_options(argc, argv, &file_name, &is_verbose);

    // Dispatch based on action
    switch (action) {
        case ACTION_CREATE:
            {
                // Files to archive come from remaining argv after getopt
                int num_files = argc - optind;
                char **file_list = &argv[optind];
                handle_create_action(file_name, file_list, num_files, is_verbose);
            }
            break;

        case ACTION_EXTRACT:
        case ACTION_TOC_SHORT:
        case ACTION_TOC_LONG:
        case ACTION_VALIDATE:
            handle_archive_reading_action(action, file_name, is_verbose);
            break;

        default:
            fprintf(stderr, "*** %s No action specified\n", argv[0]);
            exit(NO_ACTION_GIVEN);
            break;
    }

    return EXIT_SUCCESS;
}