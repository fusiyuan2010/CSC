#!/bin/sh

OPT=-O4
#OPT=-Os
DEBUG=-g -DDEBUG #-D_HAVE_PROFILER_

CFLAGS=-Wall -Wno-format -fPIC $(OPT) $(DEBUG) $(TEST) -D_7Z_TYPES_

CC=g++
INCLUDE_PATH = -I ./

OBJS=csc_analyzer.o csc_encoder_main.o csc_coder.o csc_filters.o csc_lz.o csc_memio.o csc_enc.o csc_dec.o csc_model.o csc_profiler.o csc_mf.o

LIBOUTPUT=csc.a


all: $(LIBOUTPUT) test decomp

test: test.cpp $(LIBOUTPUT)
	$(CC) $(INCLUDE_PATH) $(CFLAGS) -o $@ $^

decomp: decomp.cpp csc_dec.o csc_memio.o csc_filters.o
	$(CC) $(INCLUDE_PATH) $(CFLAGS) -o $@ $^

$(LIBOUTPUT): $(OBJS)
	ar rvs $@ $^
	
%.o: %.cpp
	$(CC) $(INCLUDE_PATH) -c $(CFLAGS) -o $@ $^

clean:
	rm -f *.o $(LIBOUTPUT) test decomp

rebuild: clean all



