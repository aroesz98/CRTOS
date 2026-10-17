/*
 * curl_config.h - libcurl 8.11 for CRTOS (arm-none-eabi, newlib-nano and libcrtos): HTTP and
 * HTTPS only, TLS from Mbed TLS, gzip/deflate from zlib, non-blocking sockets, and the
 * threaded resolver: getaddrinfo() runs in a thread of its own (libcrtos pthreads), so a
 * slow DNS answer does not stop the browser. What NetSurf does itself (HSTS) and the other
 * protocols are left out. NetSurf keeps the cookie jar too, but passes the cookies of a
 * request with CURLOPT_COOKIE, which needs curl's cookie code.
 */
#ifndef CRTOS_CURL_CONFIG_H
#define CRTOS_CURL_CONFIG_H

#define CURL_OS "arm-crtos"

/* where the trusted certificates are (the NetSurf option ca_bundle overrides it) */
#define CURL_CA_BUNDLE "/sd/crtos/etc/ssl/cacert.pem"

/* protocols and features */
#define CURL_DISABLE_ALTSVC 1
#define CURL_DISABLE_AWS 1
#define CURL_DISABLE_DICT 1
#define CURL_DISABLE_DOH 1
#define CURL_DISABLE_FILE 1             /* NetSurf reads files itself */
#define CURL_DISABLE_FORM_API 1
#define CURL_DISABLE_FTP 1
#define CURL_DISABLE_GETOPTIONS 1
#define CURL_DISABLE_GOPHER 1
#define CURL_DISABLE_HSTS 1             /* NetSurf keeps HSTS */
#define CURL_DISABLE_IMAP 1
#define CURL_DISABLE_IPFS 1
#define CURL_DISABLE_KERBEROS_AUTH 1
#define CURL_DISABLE_LDAP 1
#define CURL_DISABLE_LDAPS 1
#define CURL_DISABLE_MQTT 1
#define CURL_DISABLE_NEGOTIATE_AUTH 1
#define CURL_DISABLE_NETRC 1
#define CURL_DISABLE_NTLM 1
#define CURL_DISABLE_POP3 1
#define CURL_DISABLE_PROGRESS_METER 1
#define CURL_DISABLE_RTSP 1
#define CURL_DISABLE_SMB 1
#define CURL_DISABLE_SMTP 1
#define CURL_DISABLE_SOCKETPAIR 1
#define CURL_DISABLE_TELNET 1
#define CURL_DISABLE_TFTP 1
#define CURL_DISABLE_WEBSOCKETS 1
#define CURL_DISABLE_CA_SEARCH 1

#define USE_MBEDTLS 1
#define HAVE_LIBZ 1
#define HAVE_ZLIB_H 1

/* the asynchronous (threaded) resolver */
#define USE_THREADS_POSIX 1
#define HAVE_PTHREAD_H 1
#define HAVE_GETADDRINFO_THREADSAFE 1

/* headers */
#define STDC_HEADERS 1
#define HAVE_ARPA_INET_H 1
#define HAVE_DIRENT_H 1
#define HAVE_FCNTL_H 1
#define HAVE_LOCALE_H 1
#define HAVE_NETDB_H 1
#define HAVE_NETINET_IN_H 1
#define HAVE_NETINET_TCP_H 1
#define HAVE_POLL_H 1
#define HAVE_STDBOOL_H 1
#define HAVE_STRINGS_H 1
#define HAVE_SYS_SELECT_H 1
#define HAVE_SYS_SOCKET_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1

/* types */
#define HAVE_BOOL_T 1
#define HAVE_LONGLONG 1
#define HAVE_STRUCT_TIMEVAL 1
#define HAVE_SA_FAMILY_T 1
#define HAVE_SUSECONDS_T 1
#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define SIZEOF_LONG_LONG 8
#define SIZEOF_OFF_T 4
#define SIZEOF_CURL_OFF_T 8
#define SIZEOF_CURL_SOCKET_T 4
#define SIZEOF_SIZE_T 4
#define SIZEOF_TIME_T 8

/* functions */
#define HAVE_CLOCK_GETTIME_MONOTONIC 1
#define HAVE_FCNTL 1
#define HAVE_FCNTL_O_NONBLOCK 1
#define HAVE_FREEADDRINFO 1
#define HAVE_GETADDRINFO 1
#define HAVE_GETHOSTNAME 1
#define HAVE_GETPEERNAME 1
#define HAVE_GETSOCKNAME 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_GMTIME_R 1
#define HAVE_INET_NTOP 1
#define HAVE_INET_PTON 1
#define HAVE_OPENDIR 1
#define HAVE_POLL 1
#define HAVE_RECV 1
#define HAVE_SELECT 1
#define HAVE_SEND 1
#define HAVE_SNPRINTF 1
#define HAVE_SOCKET 1
#define HAVE_STRCASECMP 1
#define HAVE_STRDUP 1
#define HAVE_STRTOK_R 1
#define HAVE_STRTOLL 1

#endif
