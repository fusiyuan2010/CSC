#!/bin/sh

OPT=-O4
#OPT=-Os
DEBUG=-g -DDEBUG #-D_HAVE_PROFILER_

CFLAGS=-Wall -Wno-format -fPIC $(OPT) $(DEBUG) $(TEST) -D_7Z_TYPES_

CC=g++
INCLUDE_PATH = -I ./libcsc/ -I ./archiver/

LIBCSCOBJS=$(addprefix libcsc/, csc_analyzer.o csc_encoder_main.o csc_coder.o csc_filters.o csc_lz.o csc_memio.o csc_enc.o csc_dec.o csc_model.o csc_profiler.o csc_mf.o)

LIBCSCDECOBJS=$(addprefix libcsc/, csc_dec.o csc_memio.o csc_filters.o)

LIBOUTPUT=libcsc.a

all: $(LIBOUTPUT) test decomp csarc

test: ./libcsc/test.cpp $(LIBOUTPUT)
	$(CC) $(INCLUDE_PATH) $(CFLAGS) -o $@ $^

decomp: ./libcsc/decomp.cpp $(LIBCSCDECOBJS)
	$(CC) $(INCLUDE_PATH) $(CFLAGS) -o $@ $^

csarc: ./archiver/csarc.cpp $(LIBOUTPUT)
	$(CC) $(INCLUDE_PATH) $(CFLAGS) -o $@ $^ -pthread

$(LIBOUTPUT): $(LIBCSCOBJS)
	ar rvs $@ $^
	
%.o: %.cpp
	$(CC) $(INCLUDE_PATH) -c $(CFLAGS) -o $@ $^

clean:
	rm -f libcsc/*.o archiver/*.o $(LIBOUTPUT) test decomp csarc *.a

rebuild: clean all



