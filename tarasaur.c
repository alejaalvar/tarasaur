#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <zlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "tarasaur.h"

// Standard page size for efficient I/O operations
#define BUFFER_SIZE 4096

// The size of the struct on disk including compiler padding
#define PADDED_HEADER_SIZE 104

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
 * Reads a single directory entry from the archive.
 * Handles the raw byte reading and manual unpacking to account
 * for struct padding in file format.
 * 
 * @param fd - the file descriptor
 * @param header - the header to read from
 *
 * @return 0 on successful read,
 *         -1 on read failure
 */
static int 
read_directory_entry(int fd, tarasaur_directory_t *header) {
    unsigned char buffer[PADDED_HEADER_SIZE]; 
    unsigned char *ptr = buffer;

    // Perform a single read call for the entire directory entry block
    if (read(fd, buffer, PADDED_HEADER_SIZE) != PADDED_HEADER_SIZE) {
        return -1;
    }

    // 1. Name (Use the macro from tarasaur.h)
    memcpy(header->tarasaur_name, ptr, TARASAUR_MAX_NAME_LEN);         
    ptr += TARASAUR_MAX_NAME_LEN; 

    // 2. SKIP PADDING (7 bytes)
    // Compiler aligns 'size_t' (8 bytes) to offset 32. 
    // 32 - 25 (MAX_NAME_LEN) = 7 bytes of padding.
    ptr += 7; 

    // 3. Size (Use sizeof for safety)
    memcpy(&header->tarasaur_size, ptr, sizeof(size_t));         
    ptr += sizeof(size_t);

    // 4. Mode, UID, GID (Use sizeof)
    memcpy(&header->tarasaur_mode, ptr, sizeof(mode_t)); ptr += sizeof(mode_t);
    memcpy(&header->tarasaur_uid, ptr, sizeof(uid_t));  ptr += sizeof(uid_t);
    memcpy(&header->tarasaur_gid, ptr, sizeof(gid_t));  ptr += sizeof(gid_t);

    // 5. SKIP PADDING (4 bytes)
    // Compiler aligns 'timespec' (16 bytes) to offset 56.
    // 56 - 52 (current offset) = 4 bytes of padding.
    ptr += 4;

    // 6. Timespecs 
    memcpy(&header->tarasaur_atim, ptr, sizeof(struct timespec)); 
    ptr += sizeof(struct timespec);
    
    memcpy(&header->tarasaur_mtim, ptr, sizeof(struct timespec)); 
    ptr += sizeof(struct timespec);

    // 7. Offset
    memcpy(&header->tarasaur_data_offset, ptr, sizeof(off_t)); 
    ptr += sizeof(off_t);

    // 8. CRC 
    memcpy(&header->crc32_data, ptr, sizeof(uint32_t));   
    ptr += sizeof(uint32_t);
    
    memcpy(&header->crc32_header, ptr, sizeof(uint32_t)); 
    ptr += sizeof(uint32_t);

    return 0; 
}

/*
 * Process the Short Table of Contents action.
 * Iterates through the archive to print member filenames.
 *
 * @param fd - the file descriptor
 * @param member_count - the number of contained files
 * @param is_verbose - boolean flag for the verbose option
 */
static void 
do_toc_short(int fd,
             int member_count, 
             const char *archive_name, 
             bool is_verbose) {
    tarasaur_directory_t *headers = NULL;

    // 1. SKIP OVER ALL DATA BLOBS FIRST
    // The format is [Size][Data]...[Directory], so we must jump over data
    for (int i = 0; i < member_count; i++) {
        size_t current_member_size = 0;

        // Read ONLY the size (8 bytes) of the current member
        if (read(fd, &current_member_size, sizeof(size_t)) != sizeof(size_t)) {
            fprintf(stderr, "Error: Failed to read size for member %d\n", i);
            exit(READ_FAIL);
        }

        if (is_verbose) {
            fprintf(stderr, "\tskipping over data for member %d of %ld bytes\n", 
                    i, current_member_size);
        }

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

    // Allocate memory to hold all directory headers we are about to read
    headers = malloc(sizeof(tarasaur_directory_t) * member_count);
    
    if (!headers && member_count > 0) {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    // 2. READ THE DIRECTORY ENTRIES
    // We are now positioned at the end of the file where directory lives
    for (int i = 0; i < member_count; i++) {
        if (read_directory_entry(fd, &headers[i]) != 0) {
            fprintf(stderr, 
                "Error: Failed to read directory entry %d\n", 
                i);
            free(headers);
            exit(READ_FAIL);
        }
    }

    // 3. PRINT TABLE OF CONTENTS
    printf("Table of contents of tarannosaurus file: \"%s\" with %d members\n",
           archive_name ? archive_name : "stdin", member_count);

    for (int i = 0; i < member_count; i++) {
        printf("\tfile name: %s\n", headers[i].tarasaur_name);
    }

    // Clean up allocated memory
    free(headers);
}

int
main(int argc, char *argv[])
{
    int opt;
    char *file_name = NULL;
    bool is_verbose = false;
    tarasaur_action_t action = ACTION_NONE;

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

            default:
                fprintf(stderr, 
                        "Invalid command line option %c\n", 
                        opt);
                exit(INVALID_CMD_OPTION);
                break;
        }
    }

    switch (action)
    {
        case ACTION_CREATE:
            {
                fprintf(stderr, 
                        "Creating...\n");
            }
            break;
    
        case ACTION_EXTRACT:
        case ACTION_TOC_SHORT:
        case ACTION_TOC_LONG:
        case ACTION_VALIDATE:
            /*
            These actions have common error checking that
            can be collapsed into one block:
                opening a file
                check for verbose
                checking for stdin
                validate header
                validate version
                validate member count
            */
            {
                char magic_buf[128] = {0}; 
                int magic_len = strlen(TARASAUR_MAGIC_NUMBER);
                short version;
                int member_count;
                int fd;

                if (file_name) {
                    fd = open(file_name, O_RDONLY);
                    if (-1 == fd) {
                        perror("Error opening archive");
                        exit(EXIT_FAILURE);
                    }
                    // Only print for files if verbose (based on your previous output)
                    if (is_verbose) {
                        fprintf(stderr, 
                                "Reading archive file: \"%s\"\n", 
                                file_name);
                    }
                }
                else {
                    fd = STDIN_FILENO;
                    // Instructor's code prints this unconditionally for stdin
                    fprintf(stderr, 
                            "Reading archive from stdin\n");
                }

                // --- HEADER VALIDATION START ---
                
                // 1. Verify Magic Number
                // We split the check to give the specific error messages required
                if (read(fd, magic_buf, magic_len) != magic_len) {
                     // Error 1: File is too short (e.g., user typed "hello")
                     fprintf(stderr,
                             "*** failed to read magic number\n");
                     exit(BAD_MAGIC);
                }
                
                if (strcmp(magic_buf, TARASAUR_MAGIC_NUMBER) != 0) {
                    // Error 2: Content is wrong (e.g., random file)
                    fprintf(stderr, "Not a tarannosaurus file: \"%s\"\n", 
                            file_name ? file_name : "stdin");
                    exit(BAD_MAGIC);
                }

                // 2. Verify Version
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
                        do_toc_short(fd, member_count, file_name, is_verbose);
                        break;
                    
                    case ACTION_TOC_LONG:
                        // do_toc_long(fd, member_count, file_name, is_verbose);
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