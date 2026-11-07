.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

ifeq ($(origin CC),default)
CC := gcc
endif
ifeq ($(origin CXX),default)
CXX := g++
endif
AR ?= ar
BUILD ?= build
CFLAGS ?= -O2 -g
CXXFLAGS ?= -O2 -g
SANITIZER_FLAGS ?=

NCR_CPPFLAGS := -D_POSIX_C_SOURCE=200809L -D_FILE_OFFSET_BITS=64 -D_TIME_BITS=64 -Iinclude -Isrc -Itests
NCR_CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror -pthread
NCR_CXXFLAGS := -std=c++11 -Wall -Wextra -Wpedantic -Werror -pthread
NCR_LDLIBS := -pthread

# Explicit Linux sources: a foreign platform must never enter this archive.
SOURCES := src/ncr_config.c src/ncr_gps_adapter.c src/ncr_time_state.c \
           src/ncr_restore.c src/ncr_sync_service.c src/ncr_status.c \
           src/ncr_platform_linux.c
OBJECTS := $(patsubst src/%.c,$(BUILD)/%.o,$(SOURCES))
C_TESTS := test_ncr_sync test_ncr_state test_ncr_concurrency test_ncr_linux
TEST_BINS := $(addprefix $(BUILD)/,$(C_TESTS)) $(BUILD)/test_cpp $(BUILD)/test_host_gps
TEST_SUPPORT := tests/ncr_platform_fake.c tests/ncr_test_support.h

.PHONY: all test example check-platform asan ubsan tsan clean help
all: $(BUILD)/libncrsync.a

check-platform:
	@printf '%s\n' '#ifndef __linux__' '#error Ubuntu/Linux target compiler required' '#endif' '#include <time.h>' '_Static_assert(sizeof(time_t) >= 8, "64-bit time_t required");' | $(CC) $(CPPFLAGS) $(NCR_CPPFLAGS) $(CFLAGS) $(NCR_CFLAGS) -x c -fsyntax-only -

$(BUILD):
	mkdir -p $(BUILD)

$(OBJECTS): | check-platform
$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(NCR_CPPFLAGS) $(CFLAGS) $(NCR_CFLAGS) $(SANITIZER_FLAGS) -MMD -MP -c $< -o $@

$(BUILD)/libncrsync.a: $(OBJECTS)
	$(AR) rcs $@ $^

$(BUILD)/test_%: tests/test_%.c $(TEST_SUPPORT) include/ncr_sync.h include/ncr_gps_time.h $(BUILD)/libncrsync.a
	$(CC) $(CPPFLAGS) $(NCR_CPPFLAGS) $(CFLAGS) $(NCR_CFLAGS) $(SANITIZER_FLAGS) $< tests/ncr_platform_fake.c $(BUILD)/libncrsync.a $(LDFLAGS) $(NCR_LDLIBS) $(LDLIBS) -o $@

$(BUILD)/test_cpp: tests/test_cpp.cpp include/ncr_sync.h include/ncr_gps_time.h $(BUILD)/libncrsync.a
	$(CXX) $(CPPFLAGS) $(NCR_CPPFLAGS) $(CXXFLAGS) $(NCR_CXXFLAGS) $(SANITIZER_FLAGS) $< $(BUILD)/libncrsync.a $(LDFLAGS) $(NCR_LDLIBS) $(LDLIBS) -o $@

$(BUILD)/test_host_gps: tests/test_host_gps.c tests/host_gps_adapter.h src/ncr_gps_adapter.c include/ncr_sync.h include/ncr_gps_time.h src/ncr_internal.h | $(BUILD) check-platform
	$(CC) $(CPPFLAGS) $(NCR_CPPFLAGS) $(CFLAGS) $(NCR_CFLAGS) $(SANITIZER_FLAGS) -DNCR_GPS_TIME_HEADER='"host_gps_adapter.h"' tests/test_host_gps.c src/ncr_gps_adapter.c $(LDFLAGS) $(NCR_LDLIBS) $(LDLIBS) -o $@

test: $(TEST_BINS)
	$(BUILD)/test_ncr_sync
	$(BUILD)/test_ncr_state
	$(BUILD)/test_ncr_concurrency
	$(BUILD)/test_ncr_linux
	$(BUILD)/test_cpp
	$(BUILD)/test_host_gps

$(BUILD)/example_gps_callback.o: examples/example_gps_callback.c include/ncr_sync.h include/ncr_gps_time.h | $(BUILD) check-platform
	$(CC) $(CPPFLAGS) $(NCR_CPPFLAGS) $(CFLAGS) $(NCR_CFLAGS) $(SANITIZER_FLAGS) -c $< -o $@

example: $(BUILD)/libncrsync.a $(BUILD)/example_gps_callback.o

asan:
	$(MAKE) BUILD=build-asan CFLAGS='-O1 -g -fno-omit-frame-pointer' CXXFLAGS='-O1 -g -fno-omit-frame-pointer' SANITIZER_FLAGS='-fsanitize=address,undefined' test example

ubsan:
	$(MAKE) BUILD=build-ubsan CFLAGS='-O1 -g -fno-omit-frame-pointer' CXXFLAGS='-O1 -g -fno-omit-frame-pointer' SANITIZER_FLAGS='-fsanitize=undefined' test example

tsan:
	$(MAKE) BUILD=build-tsan CFLAGS='-O1 -g -fno-omit-frame-pointer' SANITIZER_FLAGS='-fsanitize=thread' build-tsan/test_ncr_concurrency
	build-tsan/test_ncr_concurrency

clean:
	rm -f -- $(OBJECTS) $(OBJECTS:.o=.d) $(BUILD)/libncrsync.a $(TEST_BINS) $(BUILD)/example_gps_callback.o

help:
	@printf '%s\n' 'make: build/libncrsync.a' 'make test example: full tests and integration example' 'make asan / ubsan / tsan: separate sanitizer builds' 'make clean: remove files in BUILD (default build)' 'make CC=clang CXX=clang++ BUILD=build-clang test example'

-include $(OBJECTS:.o=.d)
