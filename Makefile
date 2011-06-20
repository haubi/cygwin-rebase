# $Id$

Version = 3.0.1
LibVersion = $(shell fgrep RELEASE= imagehelper/Makefile.in|egrep -o '[0-9.]+')

Files = rebase.c
Objects = $(Files:.c=.o)
Libs = -L imagehelper -limagehelper
Exclude = --exclude='*.[ao]' --exclude='*.exe' --exclude='*.out' \
  	--exclude='.[A-Za-z\#]*' --exclude='*.bz2' --exclude=CVS --exclude=RCS \
	--exclude=pkgs --exclude=save --exclude=test

DESTDIR =
PREFIX = /usr
BINDIR = $(PREFIX)/bin
DOCDIR = $(PREFIX)/share/doc/Cygwin

INSTALL = install
CFLAGS = -O2 -I imagehelper

all: rebase peflags

.PHONY: imagehelper

rebase: imagehelper $(Objects)
	$(CXX) $(LDFLAGS) -o $@ $(Objects) $(Libs)

rebase.o: rebase.c Makefile
	$(CC) $(CFLAGS) -DVERSION='"$(Version)"' \
	-DLIB_VERSION='"$(LibVersion)"' \
	-c -o $@ $<

peflags: peflags.o
	$(CC) $(LDFLAGS) -o $@ peflags.o

peflags.o: peflags.c Makefile
	$(CC) $(CFLAGS) -DVERSION='"$(Version)"' \
	-c -o $@ $<

imagehelper:
	$(MAKE) -C imagehelper imagehelper

install: all
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 755 rebase.exe $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 755 peflags.exe $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 755 rebaseall $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 755 peflagsall $(DESTDIR)$(BINDIR)
	$(INSTALL) -d $(DESTDIR)$(DOCDIR)
	$(INSTALL) -m 644 README $(DESTDIR)$(DOCDIR)/rebase-$(Version).README

dist: all
	rm -f rebase-$(Version).tar.bz2
	ln -sf rebase ../rebase-$(Version)
	tar -C .. $(Exclude) -chjf rebase-$(Version).tar.bz2 rebase-$(Version)
	rm -f ../rebase-$(Version)

clean:
	$(RM) -fr *.o *.exe

realclean: clean
	$(MAKE) -C imagehelper clean
