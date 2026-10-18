CC ?= gcc
CPPFLAGS ?=
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
LDFLAGS ?=
LDLIBS ?= -pthread
PYTHON ?= python3

TARGETS := router sender
OBJECTS := router.o sender.o

.PHONY: all test clean

all: $(TARGETS)

router: router.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

sender: sender.o
	$(CC) $(LDFLAGS) -o $@ $^

router.o: router.c message.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread -c $< -o $@

sender.o: sender.c message.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread -c $< -o $@

test: all
	$(PYTHON) test_router.py ./router ./sender

clean:
	rm -f $(TARGETS) $(OBJECTS)
