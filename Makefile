CFLAGS=-g -Wall -pedantic -std=c11 
OUTFILE=gnuImage
INSTALL_DESTINATION=/usr/local/bin

default:
	gcc $(CFLAGS) gnuImage.c -lssl -lcrypto -o $(OUTFILE)

clean:
	rm $(OUTFILE)

install:
	make default
	-cp $(OUTFILE) $(INSTALL_DESTINATION)
	-chmod +x $(INSTALL_DESTINATION)/$(OUTFILE)
	echo "Done!!!"
