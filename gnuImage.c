/*
	This program is distributed as public domain.
	Author: Al Poole <netstar@gmail.com>
	
	About:
	
	Alternative to "dd".
	Can do local: 
		this if=file.iso of=/dev/sdb
		also!
		this if=http://somesite.com/music.iso of=/dev/sdc
	
	Unbuffered read/write.

*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>

void Scream(char *fmt, ...)
{
	char buf[8192] = { 0 };
	va_list ap;

	va_start(ap, fmt);
	sprintf(buf, "Nooo! %s\n", fmt);
	va_end(ap);
	fprintf(stderr, buf);

	exit(EXIT_FAILURE);
}

void Say(char *phrase)
{
	char buf[1024] = { 0 };

	sprintf(buf, "%s\n", phrase);
	printf(buf);
}


int Connect(char *hostname, int port)
{
	int sock;
	struct hostent *host;
	struct sockaddr_in host_addr;
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		Scream("socket %s\n", strerror(errno));

	host = gethostbyname(hostname);
	if (host == NULL)
		Scream("gethostbyname");

	host_addr.sin_family = AF_INET;
	host_addr.sin_port = htons(port);
	host_addr.sin_addr = *((struct in_addr *) host->h_addr);
	memset(&host_addr.sin_zero, 0, 8);

	int status =
	    connect(sock, (struct sockaddr *) &host_addr,
		    sizeof(struct sockaddr));
	if (status == 0) {
		return sock;
	}

	return 0;
}

void Usage(void)
{
	printf("ARGV[0] <from> <to> [OPTION]\n");
	printf("OPTIONS:\n");
	printf("    -bs <block size>\n");
	printf("    -h  help.\n");

	exit(EXIT_FAILURE);
}


#define CHUNK 512

char *FileFromURL(char *addr)
{
	char *str = NULL;

	char *p = addr;
	if (!p)
		Scream("broken file path");

	str = strstr(addr, "http://");
	if (str) {
		str += strlen("http://");
		char *p = strchr(str, '/');
		if (p) {
			return p;
		}
	}

	if (!p)
		Scream("FileFromURL");

	return p;
}

char *HostFromURL(char *addr)
{
	char *str = strstr(addr, "http://");
	if (str) {
		addr += strlen("http://");
		char *end = strchr(addr, '/');
		*end = '\0';
		return addr;
	}

	Scream("Invalid URL");

	return NULL;
}

#define BLOCK 1024
void Chomp(char *str)
{
	char *p = str;

	while (*p) {
		if (*p == '\r' || *p == '\n') {
			*p = '\0';
		}
		++p;
	}
}

typedef struct header_t header_t;
struct header_t {
	char location[1024];
	char content_type[1024];
	int content_length;
	char date[1024];
	int status;
};

ssize_t ReadHeader(int sock, header_t * headers)
{
	int bytes = -1;
	int len = 0;
	char buf[8192] = { 0 };
	while (1) {
		while (buf[len - 1] != '\r' && buf[len] != '\n') {
			bytes = read(sock, &buf[len], 1);
			len += bytes;

		}
		buf[len] = '\0';
		len = 0;

		sscanf(buf, "\nHTTP/1.1 %d", &headers->status);
		sscanf(buf, "\nContent-Type: %s\r", headers->content_type);
		sscanf(buf, "\nLocation: %s\r", headers->location);
		sscanf(buf, "\nContent-Length: %d\r",
		       &headers->content_length);


		if (headers->content_length && strlen(buf) == 2) {
			return 1;								  // found!!
		}

		memset(buf, 0, 8192);
	}
	return 0;										  // not found
}

int Headers(int sock, char *addr, char *file)
{
	char out[8192] = { 0 };
	header_t headers;

	memset(&headers, 0, sizeof(header_t));

	sprintf(out, "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n", file, addr);
	write(sock, out, strlen(out));

	ssize_t len = 0;

	do {
		len = ReadHeader(sock, &headers);
	} while (!len);


	if (!headers.content_length)
		Scream("bad headers!");


	return headers.content_length;
}


char *ChooseDistribution(void)
{
	struct distro_t {
		char name[1024];
		char URL[1024];
	};
	
	struct distro_t distros[9] = {
		{"OpenBSD (i386)", "http://mirror.ox.ac.uk/pub/OpenBSD/5.6/i386/install56.fs"},
		{"OpenBSD (amd64)", "http://mirror.ox.ac.uk/pub/OpenBSD/5.6/amd64/install56.fs"},
		{"FreeBSD (i386)", "ftp://ftp.freebsd.org/pub/FreeBSD/releases/ISO-IMAGES/10.1/FreeBSD-10.1-RELEASE-i386-memstick.img"},
		{"FreeBSD (amd64)", "ftp://ftp.freebsd.org/pub/FreeBSD/releases/ISO-IMAGES/10.1/FreeBSD-10.1-RELEASE-amd64-memstick.img"},		
		{"Debian (i386/amd64)", "http://caesar.acc.umu.se/debian-cd/7.7.0/multi-arch/iso-cd/debian-7.7.0-amd64-i386-netinst.iso"}, 
		{"Ubuntu (i386)", "http://releases.ubuntu.com/14.10/ubuntu-14.10-desktop-i386.iso"},
		{"Ubuntu (amd64)", "http://releases.ubuntu.com/14.10/ubuntu-14.10-desktop-amd64.iso"},
		{"LinuxMint [Cinammon] (i386)", "http://mirrors.kernel.org/linuxmint//stable/17.1/linuxmint-17.1-cinnamon-32bit.iso"},
		{"LinuxMint [Cinammon] (amd64)", "http://mirrors.kernel.org/linuxmint//stable/17.1/linuxmint-17.1-cinnamon-64bit.iso"},
	};

	int i;

	printf("Please choose an operating system to install to disk:\n\n");
	for (i = 0; i < 9; i++) {
		printf("%d) %s\n", i, distros[i].name);
	}

	printf("choice: ");
	fflush(stdout);

	char buffer[8192] = { 0 };

	fgets(buffer, sizeof(buffer), stdin);
	Chomp(buffer);

	int choice = atoi(buffer);
	
	if (choice < 0 || choice > 7)
		Scream("Invalid HUMAN input");	

	return strdup(distros[choice].URL);
}

void CheckDevices(void)
{
	char buf[8192] = { 0 };
	char result[8192] = { 0 };
	FILE *f = popen("/bin/lsblk", "r");
	if (!f) {
		return;
	}
	

	while (fgets(buf, sizeof(buf), f)) {
		if (strncmp(buf, "NAME", 4) && sscanf(buf, "%s disk", result))	{
			printf("%s\n", result);
		}
		memset(buf, 0, sizeof(buf));
	}

	pclose(f);
}

char *ChooseDevice(void)
{
	printf("Please choose a device to install to:\n\n");

	CheckDevices();
	fflush(stdout);

	char buffer[8192] = { 0 };
	printf("[DANGEROUS] Enter full device path (e.g. /dev/sdb): ");
	fflush(stdout);
	fgets(buffer, sizeof(buffer), stdin);
	Chomp(buffer);

 	struct stat fstats;

	if (stat(buffer, &fstats) < 0)
		Scream("Device does not exist!");

	return strdup(buffer);
}

int main(int argc, char **argv)
{
	unsigned long bs = CHUNK;
	char *infile = NULL;
	char *outfile = "test";
	int get_from_web = 0;

	infile = ChooseDistribution();
	if (infile) {
		get_from_web = 1;
		if (strncmp("http://", infile, 7))
			Scream("internal mess!");
	}

	outfile = ChooseDevice();
	if (!outfile)
		Scream("bogus outfile!!!");

	int in_fd, out_fd, sock;
	int length = 0;
	
	printf("Writing OS image to device %s.\n", outfile);

	if (get_from_web) {
		char *filename = strdup(FileFromURL(infile));
		char *address = strdup(HostFromURL(infile));
		if (filename && address) {
			sock = in_fd = Connect(address, 80);
			length = Headers(sock, address, filename);
		} else
			Scream("MacBorken URL");
	}

	out_fd = open(outfile, O_WRONLY | O_CREAT, 0666);
	if (out_fd < 0)
		Scream(strerror(errno));

	char buf[bs];

	memset(buf, 0, bs);

	ssize_t chunk = 0;
	ssize_t bytes = 0;

	int percent = length / 100;

	int total = 0;

	read(in_fd, buf, 1);	

	int last = 0;

	do {
		bytes = read(in_fd, buf, bs);
		if (bytes <= 0)
			break;

		chunk = bytes;

		while (chunk) {
			ssize_t count = write(out_fd, buf, chunk);
			if (count <= 0)
				break;

			chunk -= count;
			total += count;
		}

		int current = total / percent;
		if (current > 100)
			current = 100;

		if (current != last) {
			printf
		   	 ("                                                    \r");
			printf("%d%% %d bytes of %d bytes\r", current, total,
		       length);
			fflush(stdout);
		}
		last = current;
		memset(buf, 0, bytes);								  // faster
	} while (total < length);

	printf("\r\ndone!\n");

	close(out_fd);
	close(in_fd);

	return EXIT_SUCCESS;
}
