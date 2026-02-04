# Tarasaur Archive Tool

A UNIX file archiver that creates, extracts, and manages custom archive files with CRC32 validation and metadata preservation.

## Overview

Tarasaur (short for Tarannosaurus) is a file archive manager written in C that demonstrates low-level UNIX file I/O operations. It creates archive files that bundle multiple files together while preserving their permissions, timestamps, and data integrity through CRC32 checksums.

## Features

- **Create Archives** (`-c`): Bundle multiple files into a single archive
- **Extract Archives** (`-x`): Restore files with original permissions and timestamps
- **Table of Contents** (`-t`, `-T`): View archive contents (short or long format)
- **Validate Integrity** (`-V`): Verify CRC32 checksums for headers and data
- **Stdin/Stdout Support**: Work with pipes and shell redirection
- **Verbose Mode** (`-v`): Display detailed diagnostic information

## Archive Format

Tarasaur archives use a custom binary format:

```
[Magic Number: "#<tarannosaurus>\n"]
[Version: short (2 bytes)]
[Member Count: int (4 bytes)]
[Data Section: size_t + file_data pairs]
[Metadata Section: tarasaur_directory_t structures]
```

Each archive member stores:
- Filename (max 25 characters)
- File size, mode, UID, GID
- Access and modification timestamps
- Data offset within archive
- CRC32 checksums (data and header)

## Usage

```bash
tarasaur <options> [-f archive-file] [file ...]
```

### Options

| Option | Description |
|--------|-------------|
| `-c` | Create a new archive file |
| `-x` | Extract members from archive |
| `-t` | Short table of contents |
| `-T` | Long table of contents with permissions and timestamps |
| `-V` | Validate CRC32 checksums |
| `-f filename` | Specify archive filename (uses stdin/stdout if omitted) |
| `-v` | Verbose output |
| `-h` | Show help text |

### Examples

**Create an archive:**
```bash
./tarasaur -c -f myarchive.tarasaur file1.txt file2.txt file3.txt
```

**Create archive to stdout:**
```bash
./tarasaur -c file1.txt file2.txt > myarchive.tarasaur
```

**Extract from archive:**
```bash
./tarasaur -x -f myarchive.tarasaur
```

**Extract from stdin:**
```bash
cat myarchive.tarasaur | ./tarasaur -x
```

**View table of contents (short):**
```bash
./tarasaur -t -f myarchive.tarasaur
```

**View table of contents (long with metadata):**
```bash
./tarasaur -T -f myarchive.tarasaur
```

**Validate archive integrity:**
```bash
./tarasaur -V -f myarchive.tarasaur
```

**Verbose creation:**
```bash
./tarasaur -c -v -f myarchive.tarasaur file1.txt file2.txt
```

## Building

### Requirements
- GCC compiler
- GNU Make
- zlib library (`-lz`)

### Compile

```bash
make clean
make
```

Or simply:
```bash
make
```

### Makefile Targets

- `all`: Build all components (default target)
- `tarasaur`: Build the tarasaur executable
- `tarasaur.o`: Compile tarasaur.c to object file
- `clean`: Remove executables, object files, and editor backups

## Error Handling

The program handles various error conditions:
- **INVALID_CMD_OPTION** (2): Invalid command line argument
- **NO_ARCHIVE_NAME** (3): No archive filename when required
- **NO_ACTION_GIVEN** (4): No action specified on command line
- **EXTRACT_FAIL** (5): Extraction operation failed
- **CREATE_FAIL** (6): Archive creation failed
- **TOC_FAIL** (7): Table of contents operation failed
- **BAD_MAGIC** (8): Invalid magic number or version
- **READ_FAIL** (9): Read operation failed
- **VALIDATE_ERROR** (10): CRC32 validation failed

## Header File (tarasaur.h)

**Note**: This project was originally implemented for an operating systems course where the header file was provided as a symbolic link that could not be copied per assignment requirements. Therefore, it could not be properly pushed to this repository. The header file contents are included below for reference. Please include this code in the project directory and do not forget to #include it at the top of the tarasaur.c file if you are cloning this repo.

```c
// R Jesse Chaney
// rchaney@pdx.edu

#ifndef _TARASAUR_H
# define _TARASAUR_H

# define TARASAUR_MAGIC_NUMBER "#<tarannosaurus>\n"
# define TARASAUR_VERSION 1
# define TARASAUR_MAX_NAME_LEN 25

# define TARASAUR_OPTIONS "cxtTVf:vh"
// -c       Create an archive file
// -x       Extract from an archive file
// -t       Short table of contents of an archive
// -T       Long table of contents of an archive
// -V       Validate checksum/hash values
// -f name  Name of the archive file
// -v       Verbose diagnostics
// -h       Helpful info on options

typedef struct tarasaur_directory_s {
	char            tarasaur_name[TARASAUR_MAX_NAME_LEN];
	size_t          tarasaur_size;
	mode_t          tarasaur_mode;
	uid_t           tarasaur_uid;
	gid_t           tarasaur_gid;
	struct timespec tarasaur_atim; // st_atim from man 3 stat
	struct timespec tarasaur_mtim; // st_mtim from man 3 stat
	off_t           tarasaur_data_offset;

    uint32_t crc32_data;           // Calculated using zlib.
    uint32_t crc32_header;         // Calculated using zlib.
} tarasaur_directory_t;

typedef enum {
    ACTION_NONE = 0
    , ACTION_CREATE
    , ACTION_EXTRACT
    , ACTION_TOC_SHORT
    , ACTION_TOC_LONG
    , ACTION_VALIDATE
} tarasaur_action_t;


# define INVALID_CMD_OPTION 2
# define NO_ARCHIVE_NAME    3
# define NO_ACTION_GIVEN    4
# define EXTRACT_FAIL       5
# define CREATE_FAIL        6
# define TOC_FAIL           7
# define BAD_MAGIC          8
# define READ_FAIL          9
# define VALIDATE_ERROR     10


# ifndef MIN
#  define MIN(_A,_B) (((_A) < (_B)) ? (_A) : (_B))
# endif // MIN

#endif // _TARASAUR_H
```

## Author
Alejandro Alvarado