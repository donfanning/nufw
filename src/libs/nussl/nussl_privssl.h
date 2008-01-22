/*
 ** Copyright (C) 2007 INL
 ** Written by S.Tricaud <stricaud@inl.fr>
 **            L.Defert <ldefert@inl.fr>
 ** INL http://www.inl.fr/
 **
 ** $Id$
 **
 ** NuSSL: OpenSSL / GnuTLS layer based on libneon
 *
 * ChangeLog:
 * 2008-22-01: Sebastien Tricaud
 *              * Added dh parameter to nussl_ssl_context_t
 */


/*
   SSL interface definitions internal to neon.
   Copyright (C) 2003-2005, Joe Orton <joe@manyfish.co.uk>
   Copyright (C) 2004, Aleix Conchillo Flaque <aleix@member.fsf.org>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
   MA 02111-1307, USA

*/

/* THIS IS NOT A PUBLIC INTERFACE. You CANNOT include this header file
 * from an application.  */

#ifndef NUSSL_PRIVSSL_H
#define NUSSL_PRIVSSL_H

/* This is the private interface between nussl_socket, nussl_gnutls and
 * nussl_openssl. */

#include <config.h>
#include "nussl_config.h"
#include "nussl_ssl.h"
#include "nussl_socket.h"

#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>

struct nussl_ssl_context_s {
    SSL_CTX *ctx;
    SSL_SESSION *sess;
    const char *hostname; /* for SNI */

    DH *dh;
};

typedef SSL *nussl_ssl_socket;

#endif /* HAVE_OPENSSL */

#ifdef HAVE_GNUTLS

#include <gnutls/gnutls.h>

struct nussl_ssl_context_s {
    gnutls_certificate_credentials cred;
    int verify; /* non-zero if client cert verification required */
    int use_cert;

    const char *hostname; /* for SNI */

    /* Session cache. */
    union nussl_ssl_scache {
        struct {
            gnutls_datum key, data;
        } server;
#if defined(HAVE_GNUTLS_SESSION_GET_DATA2)
        gnutls_datum client;
#else
        struct {
            char *data;
            size_t len;
        } client;
#endif
    } cache;

    gnutls_dh_params_t dh;
};

typedef gnutls_session nussl_ssl_socket;

#endif /* HAVE_GNUTLS */

nussl_ssl_socket nussl__sock_sslsock(nussl_socket *sock);

/* Process-global initialization of the SSL library; returns non-zero
 * on error. */
int nussl__ssl_init(void);

/* Process-global de-initialization of the SSL library. */
void nussl__ssl_exit(void);

#endif /* NUSSL_PRIVSSL_H */
