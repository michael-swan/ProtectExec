# Helpers
dirname = $(patsubst %/,%,$(dir $1))
to_root_exe_path = $(call dirname,$(call dirname,$1))/root/$(patsubst %.c,%,$(notdir $1))

define test_root_exe
$(call to_root_exe_path,$(1)): $(1)
	$(CC) $(CFLAGS) -static -o $(call to_root_exe_path,$(1)) $(1)
endef 
# End Helpers

OUTPUT_LIB_NAME=pexec

CC=clang
CFLAGS=-std=c99 -g -O2 -Wall -Wextra -Iinc -DNDEBUG $(OPTFLAGS)
LIBS=$(OPTLIBS)
PREFIX?=/usr/local

SOURCES=$(wildcard src/**/*.c src/*.c)
OBJECTS=$(patsubst %.c,%.o,$(SOURCES))

TEST_SRC=$(wildcard test/test_*/test_*.c)
TEST_ROOT_SRC_DIR=$(wildcard test/test_*/root_src)
TEST_ROOT_SRC=$(wildcard test/test_*/root_src/*.c)
TEST_ROOT=$(foreach s,$(TEST_ROOT_SRC_DIR),$(dir $(s))root)
TEST_ROOT_EXE=$(foreach s,$(TEST_ROOT_SRC),$(call dirname,$(call dirname,$(s)))/root/$(patsubst %.c,%,$(notdir $(s))))
TEST_ROOT_SQSH=$(patsubst %,%.sqsh,$(TEST_ROOT))
TEST_EXE=$(patsubst %.c,%,$(TEST_SRC))

BUILD_DEST=build
TARGET_PREFIX=$(BUILD_DEST)/lib$(OUTPUT_LIB_NAME)
STATIC_TARGET=$(TARGET_PREFIX).a
SHARED_TARGET=$(TARGET_PREFIX).so

.PHONY: all clean static shared tests

all: static shared
clean:
	rm -rf $(BUILD_DEST) $(TEST_ROOT)
	rm -f $(OBJECTS) $(TEST_EXE) $(TEST_ROOT_SQSH)

static: $(BUILD_DEST) $(STATIC_TARGET)
shared: $(BUILD_DEST) $(SHARED_TARGET)

test_with_pax: test
	paxctl -c $(TEST_EXE)
	paxctl -m $(TEST_EXE)
	paxctl -ps $(TEST_EXE)

test: CFLAGS=-std=c99 -g -O0 -Wall -Wextra -Iinc $(OPTFLAGS)
test: $(TEST_EXE) $(TEST_ROOT) $(TEST_ROOT_SQSH)

# Compile each 'root_src/%.c' file within a test to a static executable in 'root/%'
$(foreach src,$(TEST_ROOT_SRC),$(eval $(call test_root_exe,$(src))))

# For each 'root_src/' directory within our tests, make a 'root/' directory.
$(TEST_ROOT):
	mkdir -p $@

test/test_%/root.sqsh: $(TEST_ROOT_EXE)
	rm -f $@
	mksquashfs $< $@ -all-root

test/test_%: test/test_%.c $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $^

$(STATIC_TARGET): $(OBJECTS)
	ar rcs $@ $(OBJECTS)
	ranlib $@

$(SHARED_TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -shared -o $@ $(OBJECTS)

$(BUILD_DEST):
	mkdir -p $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<
