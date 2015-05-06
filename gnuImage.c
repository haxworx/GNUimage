/*
	Instant ISO/IMG URL to Disk Drive.
	
	Images up-to-date as of: 2015-05-01
.
	email: Al Poole <netstar@gmail.com>
	www: http://haxlab.org
	
	You've heard of unetbootin? This one is for the command-line.
	
	Supports: HTTP and HTTPS
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

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>



void Scream(char *fmt, ...)
{
	char buf[8192] = { 0 };
	va_list ap;

	va_start(ap, fmt);
	snprintf(buf, sizeof(buf), "Nooo! %s\n", fmt);
	va_end(ap);
	fprintf(stderr, buf);

	exit(EXIT_FAILURE);
}

void init_ssl(void)
{
	SSL_load_error_strings();
	ERR_load_BIO_strings();
	OpenSSL_add_all_algorithms();
}

void Say(char *phrase)
{
	char buf[1024] = { 0 };

	snprintf(buf, sizeof(buf), "%s\n", phrase);
	printf(buf);
}

BIO *Connect_SSL(char *hostname, int port)
{
	BIO *bio = NULL;
	char bio_addr[8192] = { 0 };

	snprintf(bio_addr, sizeof(bio_addr), "%s:%d", hostname, port);
	
	SSL_library_init(); // missing?

	SSL_CTX *ctx = SSL_CTX_new(SSLv23_client_method());
	SSL *ssl;
	
	
	SSL_CTX_load_verify_locations(ctx, "/etc/ssl/certs", NULL);
		
	bio = BIO_new_ssl_connect(ctx);
	if (bio == NULL)
		Scream("BIO_new_ssl_connect");

	
	BIO_get_ssl(bio, &ssl);
	SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
	BIO_set_conn_hostname(bio, bio_addr);

	if (BIO_do_connect(bio) <= 0)
		Scream("Unable to connect");

	return bio;
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
	char *end = NULL;

	char *str = strstr(addr, "http://");
	if (str) {
		addr += strlen("http://");
		end = strchr(addr, '/');
		*end = '\0';
		return addr;
	} 

	str = strstr(addr, "https://");
	if (str) {
		addr += strlen("https://");
		end = strchr(addr, '/');
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

ssize_t ReadHeader(int sock, BIO *bio, header_t * headers)
{
	int bytes = -1;
	int len = 0;
	char buf[8192] = { 0 };
	while (1) {
		while (buf[len - 1] != '\r' && buf[len] != '\n') {
			if (bio == NULL)			
				bytes = read(sock, &buf[len], 1);
			else
				bytes = BIO_read(bio, &buf[len], 1);
			
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

int Headers(int sock, BIO *bio, char *addr, char *file)
{
	char out[8192] = { 0 };
	header_t headers;

	memset(&headers, 0, sizeof(header_t));

	snprintf(out, sizeof(out), "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n", file, addr);

	ssize_t len = 0;

	if (bio == NULL) {
		len = write(sock, out, strlen(out));
	} else {
		len = BIO_write(bio, out, strlen(out));
	}

	len = 0;

	do {
		len = ReadHeader(sock, bio, &headers);
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
		int is_ssl;
	};
	
	#define NUM_DISTROS 13


	char *timestamp = "2015-05-01";

	struct distro_t distros[NUM_DISTROS] = {
		{"NetBSD v6.1.5 (x86)", "http://mirror.planetunix.net/pub/NetBSD/iso/6.1.5/NetBSD-6.1.5-i386.iso", 0},
		{"NetBSD v6.1.5 (x86_64)", "http://mirror.planetunix.net/pub/NetBSD/iso/6.1.5/NetBSD-6.1.5-amd64.iso", 0},
		{"OpenBSD v5.7 (x86)", "http://mirror.ox.ac.uk/pub/OpenBSD/5.7/i386/install57.fs", 0},
		{"OpenBSD v5.7 (x86_64)", "http://mirror.ox.ac.uk/pub/OpenBSD/5.7/amd64/install57.fs", 0},
		{"FreeBSD v10.1 (x86)", "http://ftp.freebsd.org/pub/FreeBSD/releases/ISO-IMAGES/10.1/FreeBSD-10.1-RELEASE-i386-memstick.img", 0},
		{"FreeBSD v10.1 (x86_64)", "http://ftp.freebsd.org/pub/FreeBSD/releases/ISO-IMAGES/10.1/FreeBSD-10.1-RELEASE-amd64-memstick.img", 0},		
		{"Debian v8.0 (x86/x86_64)", "http://caesar.acc.umu.se/debian-cd/8.0.0/multi-arch/iso-cd/debian-8.0.0-amd64-i386-netinst.iso", 0}, 
		{"Fedora v21 (x86)", "http://www.mirrorservice.org/sites/dl.fedoraproject.org/pub/fedora/linux/releases/21/Workstation/i386/iso/Fedora-Live-Workstation-i686-21-5.iso", 0},		
		{"Fedora v21 (x86_64)", "http://www.mirrorservice.org/sites/dl.fedoraproject.org/pub/fedora/linux/releases/21/Workstation/x86_64/iso/Fedora-Live-Workstation-x86_64-21-5.iso", 0},		
		{"OpenSUSE v13.2 (x86)", "http://anorien.csc.warwick.ac.uk/mirrors/download.opensuse.org/distribution/13.2/iso/openSUSE-13.2-DVD-i586.iso", 0},
		{"OpenSUSE v13.2 (x86_64)","http://anorien.csc.warwick.ac.uk/mirrors/download.opensuse.org/distribution/13.2/iso/openSUSE-13.2-DVD-x86_64.iso", 0},
		{"LinuxMint v17.1 [Cinammon] (x86)", "https://mirrors.kernel.org/linuxmint//stable/17.1/linuxmint-17.1-cinnamon-32bit.iso", 1 },
		{"LinuxMint v17.1 [Cinammon] (x86_64)", "https://mirrors.kernel.org/linuxmint//stable/17.1/linuxmint-17.1-cinnamon-64bit.iso", 1},
	};

	int i;

	printf("Brought to you by: \"Al Poole\" <netstar@gmail.com>. Updated %s.\n\n", timestamp);
	printf("\nPlease choose an operating system to install to disk or file:\n\n");
	for (i = 0; i < NUM_DISTROS; i++) {
		printf("%2.1d) %s\n", i, distros[i].name);
	}

	printf("\nchoice: ");
	fflush(stdout);

	char buffer[8192] = { 0 };

	fgets(buffer, sizeof(buffer), stdin);
	Chomp(buffer);

	int choice = atoi(buffer);
	
	if (choice < 0 || choice > NUM_DISTROS)
		Scream("Choice out of range!");	

	return strdup(distros[choice].URL);
}

void CheckDevices(void)
{
	return; // BSD doesn't have lsblk
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
	printf("[DANGEROUS] Enter path or press RETURN to save to current directory: ");
	fflush(stdout);
	fgets(buffer, sizeof(buffer), stdin);
	Chomp(buffer);

 	struct stat fstats;

	if (stat(buffer, &fstats) < 0)
		return NULL;

	return strdup(buffer);
}

char *GetFileName(char *URI)
{
	char *uri_copy = strdup(URI);

	char *new_start = strrchr(uri_copy, '/');
	if (new_start == NULL)
		Scream("Program is broken!");

	*new_start = '\0'; ++new_start;

	return strdup(new_start);
}

int main(int argc, char **argv)
{
	unsigned long bs = CHUNK;
	char *infile = NULL;
	char *outfile = "test";
	int get_from_web = 0;
	int is_ssl = 0;

	infile = ChooseDistribution();
	if (infile) {
		get_from_web = 1;
		if (!strncmp("https://", infile, 8))
			is_ssl = 1;

		if (strncmp("http://", infile, 7) && strncmp("https://", infile, 8))
			Scream("Bad URL");
	}

	if (is_ssl)
		init_ssl();

	outfile = ChooseDevice();
	if (!outfile)
		outfile = GetFileName(infile);

	int in_fd, out_fd, sock;
	int length = 0;
	
	printf("Writing OS image to %s.\n", outfile);

	BIO *bio = NULL;

	if (get_from_web) {
		char *filename = strdup(FileFromURL(infile));
		char *address = strdup(HostFromURL(infile));
		if (filename && address) {
			if (!is_ssl)
				sock = in_fd = Connect(address, 80);
			else
				bio = Connect_SSL(address, 443);

			length = Headers(sock, bio, address, filename);		
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

	if (bio)
		BIO_read(bio, buf, 1);
	else	
		read(in_fd, buf, 1);	

	unsigned char result[SHA256_DIGEST_LENGTH] = { 0 };
	SHA256_CTX ctx;
	
	SHA256_Init(&ctx);

	do {

		if (bio == NULL)
			bytes = read(in_fd, buf, bs);	
		else
			bytes = BIO_read(bio, buf, bs);
			if (bytes <= 0)
				break;

		chunk = bytes;

		SHA256_Update(&ctx, buf, bytes);

		while (chunk) {
			ssize_t count = 0;
			count = write(out_fd, buf, chunk);
			
			if (count <= 0)
				break;

			chunk -= count;
			total += count;
		}

		int current = total / percent;
		if (current > 100)
			current = 100;

		printf
	   	 ("                                                    \r");
		printf("%d%% %d bytes of %d bytes\r", current, total,
		       length);
		fflush(stdout);

		memset(buf, 0, bytes);								  // faster
	} while (total < length);

	SHA256_Final(result, &ctx);

	int i = 0;

	printf("SHA256 (%s) = ", outfile); fflush(stdout);
	for (i = 0; i < SHA256_DIGEST_LENGTH; i++)
		printf("%02x", (unsigned int) result[i]);

	printf("\n");
	printf("done!\n");

	BIO_free_all(bio);
	
	close(out_fd);
	close(in_fd);

	return EXIT_SUCCESS;
}
