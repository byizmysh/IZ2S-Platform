DEVKITPRO ?= $(abspath ../devkitpro)
export DEVKITPRO
CC := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-gcc
AR := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-ar
CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -mcpu=cortex-a78c+crypto -mtp=soft -fPIE -ffunction-sections -fdata-sections -D__SWITCH__ -D__SWITCH2__ -Iinclude -isystem $(DEVKITPRO)/libounce/include
OBJECTS := obj/profile.o obj/services.o obj/audio.o obj/compat.o obj/hid.o obj/input.o obj/storage.o obj/storage-files.o obj/stream.o obj/memory.o
.PHONY: all check
.DELETE_ON_ERROR:
all: lib/libsb4.a lib/libsb4-host-heap.a lib/libsb4-host-runtime.a lib/libsb4-host-posix.a
obj lib:
	mkdir -p $@
obj/%.o: source/%.c $(wildcard include/sb4/*.h) include/sb4.h Makefile | obj
	$(CC) $(CFLAGS) -c $< -o $@
obj/hid.o obj/services.o: source/hid.h
obj/compat.o obj/host-posix.o: source/host-posix.def source/host-clock.h
obj/compat.o: source/host-timed-mutex.h source/native-services.def source/application-io.def
lib/libsb4.a: $(OBJECTS) | lib
	$(AR) rcs $@ $^
lib/libsb4-host-heap.a: obj/host-heap.o | lib
	$(AR) rcs $@ $^
lib/libsb4-host-runtime.a: obj/host-runtime.o | lib
	$(AR) rcs $@ $^
lib/libsb4-host-posix.a: obj/host-posix.o | lib
	$(AR) rcs $@ $^
obj/link.elf: tests/link.c lib/libsb4.a
	$(CC) $(CFLAGS) -specs=$(DEVKITPRO)/libounce/switch.specs -L$(DEVKITPRO)/libounce/lib $< lib/libsb4.a -lounce -lm -o $@
check: all obj/link.elf
obj/audio-test: tests/audio.c tests/switch.h source/audio.c include/sb4/audio.h | obj
	cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Itests -Iinclude tests/audio.c source/audio.c -o $@
obj/iznes-test: tests/iznes.c tests/switch.h adapters/iznes/native.c source/audio.c $(wildcard include/sb4/*.h) | obj
	cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Itests -Iinclude $(filter %.c,$^) -o $@
obj/music-bulk-test: tests/music-bulk.c tests/switch.h adapters/music-bulk/native.c source/audio.c $(wildcard include/sb4/*.h) | obj
	cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Itests -Iinclude $(filter %.c,$^) -o $@
obj/compat-test: tests/compat.c tests/switch.h source/compat.c source/stream.c source/host-heap.c $(wildcard include/sb4/*.h) | obj
	cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Itests -Iinclude $(filter %.c,$^) -lm -o $@
obj/hid-test: tests/hid.c tests/switch.h source/hid.c source/hid.h source/input.c include/sb4/services.h include/sb4/input.h | obj
	cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Itests -Iinclude $(filter %.c,$^) -o $@
obj/storage-test: tests/storage.c tests/switch.h source/storage.c source/storage-files.c include/sb4/storage.h include/sb4/storage-provider.h | obj
	cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -Itests -Iinclude tests/storage.c source/storage.c source/storage-files.c -o $@
obj/host-runtime-test: tests/host-runtime.c tests/switch.h source/compat.c source/stream.c source/host-heap.c source/host-runtime.c $(wildcard include/sb4/*.h) | obj
	cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Itests -Iinclude $(filter %.c,$^) -lm -o $@
obj/host-posix-test: tests/host-posix.c source/host-posix.c source/host-posix.def source/host-clock.h source/host-timed-mutex.h include/sb4/host-posix.h include/sb4/compat.h Makefile | obj
	cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -Iinclude $(filter %.c,$^) -o $@
check: obj/audio-test obj/iznes-test obj/music-bulk-test obj/compat-test obj/hid-test obj/storage-test obj/host-runtime-test obj/host-posix-test
	python3 tests/check.py
	./obj/audio-test
	./obj/audio-test retained
	./obj/iznes-test
	./obj/iznes-test retained
	./obj/music-bulk-test
	./obj/music-bulk-test retained
	./obj/compat-test
	./obj/hid-test
	./obj/storage-test
	./obj/host-runtime-test
	./obj/host-runtime-test retained
	./obj/host-posix-test
	./obj/host-posix-test original-provider
	./obj/host-posix-test detached
	./obj/host-posix-test detached-at-create
