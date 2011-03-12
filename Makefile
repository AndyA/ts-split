.PHONY: all clean dist install

INCLUDES=-I/usr/local/include
LIBS=-L/usr/local/lib -lavformat -lavcodec
LDFLAGS=$(LIBS)
CPPFLAGS=-Wall -O3 $(INCLUDES)
OBJS=ts-split.o
PROG=ts-split
INSTALL_PREFIX=/usr/local

all: $(PROG)

$(PROG): $(OBJS)

clean:
	rm -rf $(OBJS) $(PROG) *.dSYM $(PROG)-*.tar.gz


install: $(PROG)
	mkdir -p $(INSTALL_PREFIX)/bin
	cp $(PROG) $(INSTALL_PREFIX)/bin

