.PHONY: all clean dist install

FFMPEG=$(shell which ffmpeg)
BINDIR=$(shell dirname $(FFMPEG))
PREFIX=$(shell dirname $(BINDIR))

INCLUDES=-I$(PREFIX)/include
LIBS=-L$(PREFIX)/lib -lavcore -lavcodec -lavformat -lavutil -lmp3lame -lvpx -lx264 -lz -lfaac -lswscale -lbz2
LDFLAGS=$(LIBS)
# CPPFLAGS=-Wall -O3 $(INCLUDES)
CPPFLAGS=-Wall -g $(INCLUDES)
OBJS=ts-split.o
PROG=ts-split
INSTALL_PREFIX=$(PREFIX)

LDFLAGS+=-lgcov
CPPFLAGS+=-fprofile-arcs -ftest-coverage

all: $(PROG)

$(PROG): $(OBJS)

clean:
	rm -rf $(OBJS) $(PROG) *.dSYM $(PROG)-*.tar.gz
	rm -f *.gcov *.gcda *.gcno

install: $(PROG)
	mkdir -p $(INSTALL_PREFIX)/bin
	cp $(PROG) $(INSTALL_PREFIX)/bin

