OUTPUT_LIB_NAME=pexec

CC=gcc
CFLAGS=-g -O2 -Wall -Wextra -Iinc -DNDEBUG $(OPTFLAGS)
LIBS=$(OPTLIBS)
PREFIX?=/usr/local

SOURCES=$(wildcard src/**/*.c src/*.c)
OBJECTS=$(patsubst %.c,%.o,$(SOURCES))

TEST_SRC=$(wildcard test/test_*.c)
TESTS=$(patsubst %.c,%,$(TEST_SRC))

BUILD_DEST=build
TARGET_PREFIX=$(BUILD_DEST)/lib$(OUTPUT_LIB_NAME)
STATIC_TARGET=$(TARGET_PREFIX).a
SHARED_TARGET=$(TARGET_PREFIX).so

.PHONY: all clean static shared tests

all: static shared
clean:
	rm -rf $(BUILD_DEST)
	rm -f $(OBJECTS) $(TESTS)

static: $(BUILD_DEST) $(STATIC_TARGET)
shared: $(BUILD_DEST) $(SHARED_TARGET)

tests: CFLAGS=-g -O0 -Wall -Wextra -Iinc -DNDEBUG $(OPTFLAGS)
tests: $(TESTS)

test/test_%: test/test_%.c $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $^
	paxctl -c $@
	paxctl -m $@
	paxctl -ps $@

$(STATIC_TARGET): $(OBJECTS)
	ar rcs $@ $(OBJECTS)
	ranlib $@

$(SHARED_TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -shared -o $@ $(OBJECTS)

$(BUILD_DEST):
	mkdir -p $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<
