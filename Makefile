CFLAGS=-g -Wall -pedantic -std=c99
LDFLAGS= -lssl -lcrypto
SOURCES=newimage.c
OUTFILE=newimage
INSTALL_PREFIX=/usr/local

default:
	$(CC) $(CFLAGS) $(LDFLAGS) $(SOURCES) -o $(OUTFILE)
clean:
	-rm $(OUTFILE) 
	-rm *.iso *.fs
install:
	-install -m 0755 $(OUTFILE) $(INSTALL_PREFIX)/bin
