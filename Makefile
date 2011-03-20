.PHONY: all clean install tags

FFMPEG=$(shell which ffmpeg)
BINDIR=$(shell dirname $(FFMPEG))
PREFIX=$(shell dirname $(BINDIR))
INCDIR=$(PREFIX)/include
LIBDIR=$(PREFIX)/lib

INCLUDES=-I$(INCDIR) -I/opt/local/include 
LIBS=-lavcore -lavcodec -lavformat -lavutil -lmp3lame -lvpx -lx264 -lz -lfaac -lswscale -lbz2 -lgcrypt
LDFLAGS=-L/opt/local/lib $(LIBS) -L$(LIBDIR) 
CPPFLAGS=-Wall -O3 $(INCLUDES)
# CPPFLAGS=-Wall -g $(INCLUDES)
OBJS=ts-split.o
PROG=ts-split
INSTALL_PREFIX=$(PREFIX)

# LDFLAGS+=-lgcov
# CPPFLAGS+=-fprofile-arcs -ftest-coverage

all: $(PROG)

$(PROG): $(OBJS)

clean:
	rm -rf $(OBJS) $(PROG) *.dSYM $(PROG)-*.tar.gz
	rm -f *.gcov *.gcda *.gcno

install: $(PROG)
	mkdir -p $(INSTALL_PREFIX)/bin
	cp $(PROG) $(INSTALL_PREFIX)/bin

tags:
	ctags -R ../ffmpeg/ffmpeg .
