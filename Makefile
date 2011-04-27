.PHONY: all clean install tags

FFMPEG=$(shell which ffmpeg)
BINDIR=$(shell dirname $(FFMPEG))
PREFIX=$(shell dirname $(BINDIR))
INCDIR=$(PREFIX)/include
LIBDIR=$(PREFIX)/lib

LIBS=-lavcodec -lavformat -lavutil \
	-lmp3lame -lvpx -lx264 \
	-lz -lfaac -lswscale -lbz2 -lgcrypt

DBPREFIX=$(PWD)/ffmpeg
DBINCDIR=$(DBPREFIX)/include
DBLIBDIR=$(DBPREFIX)/lib

# Extra libs needed for static linking.
DBLIBS=-logg -lvorbis -lvorbisenc -ltheora -ltheoraenc -ltheoradec

INCLUDES=-I/opt/local/include -I$(INCDIR)
LDFLAGS=-L/opt/local/lib $(LIBS) -L$(LIBDIR)
CPPFLAGS=-Wall -O3 $(INCLUDES)

# INCLUDES=-I/opt/local/include -I$(DBINCDIR) -I$(INCDIR)
# LDFLAGS=-L/opt/local/lib $(DBLIBS) $(LIBS) -L$(DBLIBDIR) -L$(LIBDIR)
# CPPFLAGS=-Wall -g $(INCLUDES)
# LDFLAGS+=-lgcov
# CPPFLAGS+=-fprofile-arcs -ftest-coverage

OBJS=ts-split.o
PROG=ts-split
INSTALL_PREFIX=$(PREFIX)


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
