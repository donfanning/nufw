noinst_LTLIBRARIES = libnussl.la

libnussl_la_SOURCES = nussl_alloc.c nussl_dates.c nussl_openssl.c nussl_gnutls.c nussl_session.c nussl_socket.c nussl_string.c nussl_utils.c
AM_CFLAGS = -I$(top_srcdir)/src/include/

if USE_OPENSSL
AM_CFLAGS=$(AM_CFLAGS) @OPENSSL_CFLAGS@
AM_LDFLAGS=@OPENSSL_LIBS@
endif
