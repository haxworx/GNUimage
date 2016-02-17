/*

   ...installer for Debian GNU/Linux and the
   ...good BSDs (from GNU/Linux)...
   ......meh!!!
   .........Al Poole <netstar@gmail.com>
   ............http://haxlab.org

   wrote this to play with HTTPS/1.1 

   there are better ways to do this...
   e.g. (/bin/sh et. al.)

*/

#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>

char *strdup(const char *s);

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#define h_addr h_addr_list[0]

void Error(char *fmt, ...)
{
    char buf[8192] = { 0 };
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    fprintf(stderr,"%s\n", buf);

    exit(EXIT_FAILURE);
}

void init_ssl(void)
{
    SSL_load_error_strings();
    ERR_load_BIO_strings();
    OpenSSL_add_all_algorithms();
}

void Say(char *something)
{
    char buf[1024] = { 0 };

    snprintf(buf, sizeof(buf), "%s\n", something);
    puts(buf);
}

BIO *connect_ssl(char *hostname, int port)
{
    BIO *bio = NULL;
    char bio_addr[8192] = { 0 };

    snprintf(bio_addr, sizeof(bio_addr), "%s:%d", hostname, port);
    
    SSL_library_init(); 

    SSL_CTX *ctx = SSL_CTX_new(SSLv23_client_method());
    SSL *ssl = NULL;
    
    SSL_CTX_load_verify_locations(ctx, "/etc/ssl/certs", NULL);
        
    bio = BIO_new_ssl_connect(ctx);
    if (!bio)
        Error("BIO_new_ssl_connect");

    
    BIO_get_ssl(bio, &ssl);
    SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
    BIO_set_conn_hostname(bio, bio_addr);

    if (BIO_do_connect(bio) <= 0)
        Error("BIO_do_connect()!");

    return bio;
}

int connect_tcp(char *hostname, int port)
{
    int sock;
    struct hostent *host;
    struct sockaddr_in host_addr;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        Error("socket() %s", strerror(errno));

    host = gethostbyname(hostname);
    if (!host)
        Error("gethostbyname()");

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

void usage(void)
{
    printf("ARGV[0] <from> <to> [OPTION]\n");
    printf("OPTIONS:\n");
    printf("    -bs <block size>\n");
    printf("    -h  help.\n");

    exit(EXIT_FAILURE);
}


#define CHUNK 512

char *file_from_url(char *addr)
{
    char *str = NULL;

    char *p = addr;
    if (!p)
        Error("broken file path");

    str = strstr(addr, "http://");
    if (str) {
        str += strlen("http://");
        char *p = strchr(str, '/');
        if (p) {
            return p;
        }
    }

    if (!p)
        Error("file_from_url");

    return p;
}

char *hostname_from_url(char *addr)
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

    Error("Invalid url");

    return NULL;
}

#define BLOCK 1024
void trimmy(char *str)
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

ssize_t check_one_http_header(int sock, BIO *bio, header_t * headers)
{
    int bytes = -1;
    int len = 0;
    char buf[8192] = { 0 };
    while (1) {
        while (buf[len - 1] != '\r' && buf[len] != '\n') {
            if (!bio)            
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
            return 1;                                  // found!!
        }

        memset(buf, 0, 8192);
    }
    return 0;                                          // not found
}

int check_some_http_headers(int sock, BIO *bio, char *addr, char *file)
{
    char out[8192] = { 0 };
    header_t headers;

    memset(&headers, 0, sizeof(header_t));

    snprintf(out, sizeof(out), "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n", file, addr);

    ssize_t len = 0;

    if (!bio) {
        len = write(sock, out, strlen(out));
    } else {
        len = BIO_write(bio, out, strlen(out));
    }

    len = 0;

    do {
        len = check_one_http_header(sock, bio, &headers);
    } while (!len);

    if (!headers.content_length)
        Error("BAD BAD HTTP HEADERS!");

    return headers.content_length;
}


char *choose_an_os(void)
{
    struct distro_t {
        char name[1024];
        char url[1024];
        int is_ssl;
    };

    char *timestamp = "2016-02-16";

    #define COUNT 9
    struct distro_t distros[] = {
        {"Debian GNU/Linux v8.3 (i386/amd64)", "http://gensho.acc.umu.se/debian-cd/8.3.0/multi-arch/iso-cd/debian-8.3.0-amd64-i386-netinst.iso", 0},
        {"FreeBSD v10.2 (x86)", "http://ftp.freebsd.org/pub/FreeBSD/releases/ISO-IMAGES/10.2/FreeBSD-10.2-RELEASE-i386-memstick.img", 0},
        {"FreeBSD v10.2 (amd64)", "http://ftp.freebsd.org/pub/FreeBSD/releases/ISO-IMAGES/10.2/FreeBSD-10.2-RELEASE-amd64-memstick.img", 0},
        {"NetBSD v7.0 (i386)", "http://mirror.planetunix.net/pub/NetBSD/iso/7.0/NetBSD-7.0-i386.iso", 0},
        {"NetBSD v7.0 (amd64)", "http://mirror.planetunix.net/pub/NetBSD/iso/7.0/NetBSD-7.0-amd64.iso", 0},
        {"OpenBSD v5.8 (i386)", "http://mirror.ox.ac.uk/pub/OpenBSD/5.7/i386/install57.fs", 0},
        {"OpenBSD v5.8 (amd64)", "http://mirror.ox.ac.uk/pub/OpenBSD/5.7/amd64/install57.fs", 0},
        {"OpenBSD v5.9 (snapshot) (i386)", "http://mirror.ox.ac.uk/pub/OpenBSD/snapshots/i386/install59.fs", 0},
        {"OpenBSD v5.9 (snapshot) (amd64)", "http://mirror.ox.ac.uk/pub/OpenBSD/snapshots/amd64/install59.fs", 0},
        
    };

    int i;

    printf("Brought to you by: \"Al Poole\" <netstar@gmail.com>. Updated %s.\n\n", timestamp);
    for (i = 0; i < COUNT; i++) {
        printf("%2.1d) %s\n", i, distros[i].name);
    }

    printf("\nchoice: ");
    fflush(stdout);

    char buffer[8192] = { 0 };

    fgets(buffer, sizeof(buffer), stdin);
    trimmy(buffer);

    int choice = atoi(buffer);
    
    if (choice < 0 || choice > COUNT)
        Error("Choice out of range!");    

    char *url = strdup(distros[choice].url);
    return url;
}

void check_for_devices(void)
{
    char buf[8192] = { 0 };
    char result[8192] = { 0 };
    FILE *f = popen("/bin/lsblk", "r");
    if (!f) {
        return;
    }

    while (fgets(buf, sizeof(buf), f)) {
        if (strncmp(buf, "NAME", 4) && sscanf(buf, "%s disk", result))    {
            printf("%s\n", result);
        }
        memset(buf, 0, sizeof(buf));
    }

    pclose(f);

}

char *choose_install_device(void)
{
    printf("Please choose a device to install to:\n\n");

    fflush(stdout);

    char buffer[8192] = { 0 };

    printf("[DANGEROUS] Enter path or press RETURN to save to current directory: ");

    fflush(stdout);
    fgets(buffer, sizeof(buffer), stdin);
    trimmy(buffer);

    struct stat fstats;

    if (stat(buffer, &fstats) < 0)
        return NULL;

    return strdup(buffer);
}

char *file_name_from_uri(char *uri)
{
    char *uri_copy = strdup(uri);

    char *new_start = strrchr(uri_copy, '/');
    if (!new_start)
        Error("file_name_from_uri(%s)", strerror(errno));

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

    infile = choose_an_os();
    if (infile) {
        get_from_web = 1;
        if (!strncmp("https://", infile, 8))
            is_ssl = 1;

        if (strncmp("http://", infile, 7) && strncmp("https://", infile, 8))
            Error("strncmp(Bad url)");
    }

    if (is_ssl)
        init_ssl();

    outfile = choose_install_device();
    if (!outfile)
        outfile = file_name_from_uri(infile);

    int in_fd, out_fd, sock;
    int length = 0;
    
    printf("Writing OS image to: %s.\n", outfile);

    BIO *bio = NULL;

    if (get_from_web) {
        char *filename = strdup(file_from_url(infile));
        char *address = strdup(hostname_from_url(infile));
        if (filename && address) {
            if (!is_ssl)
                sock = in_fd = connect_tcp(address, 80);
            else
                bio = connect_ssl(address, 443);

            length = check_some_http_headers(sock, bio, address, filename);        
        } else
            Error("this is surely broken!!!");
    }

    out_fd = open(outfile, O_WRONLY | O_CREAT, 0666);
    if (out_fd < 0)
        Error("open: %s", strerror(errno));

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

        memset(buf, 0, bytes);                                  // faster
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
