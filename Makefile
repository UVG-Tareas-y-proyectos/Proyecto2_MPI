CC = gcc
MPICC ?= mpicc
CPPFLAGS += -Iinclude $(shell pkg-config --cflags openssl)
CFLAGS ?= -O3 -std=c11 -Wall -Wextra -Wpedantic
LDLIBS += $(shell pkg-config --libs openssl)

.PHONY: all clean test measure
all: build/des_tool build/bruteforce_mpi

build:
	mkdir -p build

build/des_tool: src/des_tool.c src/des_common.c include/des_common.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) src/des_tool.c src/des_common.c -o $@ $(LDLIBS)

build/bruteforce_mpi: src/bruteforce.c src/des_common.c include/des_common.h | build
	$(MPICC) $(CPPFLAGS) $(CFLAGS) src/bruteforce.c src/des_common.c -o $@ $(LDLIBS)

test: all
	python3 scripts/verify.py

measure: all
	python3 scripts/measure.py

clean:
	rm -f build/des_tool build/bruteforce_mpi
