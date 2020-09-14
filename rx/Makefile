CFLAGS = -O3 -Wall -static -Wno-unknown-pragmas
LDLIBS = -lorcon -lm -lrt
CC = gcc

ALL = log_to_file

all: $(ALL)

clean:
	rm -f *.o $(ALL)

log_to_file: log_to_file.c util_pay.o iwl_connector.h

util_pay.c: util_pay.h

