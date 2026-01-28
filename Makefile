CC = gcc

CFLAGS = -Wall -Wextra -Wshadow -Wunreachable-code -Wredundant-decls \
         -Wmissing-declarations -Wold-style-definition \
         -Wmissing-prototypes -Wdeclaration-after-statement \
         -Wno-return-local-addr -Wunsafe-loop-optimizations \
         -Wuninitialized -Werror

TAR_FILE = Lab2_${LOGNAME}.tar.gz

.PHONY: all clean

all: tarasaur

tarasaur: tarasaur.o
	$(CC) $(CFLAGS) -o tarasaur tarasaur.o

tarasaur.o: tarasaur.c
	$(CC) $(CFLAGS) -c tarasaur.c

clean:
	rm -f tarasaur *.o *~ \#*

tar: clean
	rm -f $(TAR_FILE)
	tar cvfa $(TAR_FILE) *.[ch] ?akefile

