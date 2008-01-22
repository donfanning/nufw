/*
 ** Copyright (C) 2007 INL
 ** Written by S.Tricaud <stricaud@inl.fr>
 **            L.Defert <ldefert@inl.fr>
 ** INL http://www.inl.fr/
 **
 ** $Id$
 **
 ** NuSSL: OpenSSL / GnuTLS layer based on libneon
 */


/*
   neon SSL/TLS support using GNU TLS
   Copyright (C) 2007, Joe Orton <joe@manyfish.co.uk>
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

#include "config.h"
#include "nussl_config.h"
#include "nussl_ssl_common.h"

#ifdef HAVE_GNUTLS

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include <gnutls/gnutls.h>
#include <gnutls/pkcs12.h>

#ifdef NUSSL_HAVE_TS_SSL
#include <errno.h>
#include <pthread.h>
#include <gcrypt.h>
GCRY_THREAD_OPTION_PTHREAD_IMPL;

#endif

#ifdef HAVE_ICONV
#include <iconv.h>
#endif

#include "nussl_ssl.h"
#include "nussl_string.h"
#include "nussl_session.h"
#include "nussl_internal.h"

#include "nussl_private.h"
#include "nussl_privssl.h"
#include "nussl_utils.h"

#define UGLY_DEBUG() printf("%s %s:%i\n", __FUNCTION__, __FILE__, __LINE__)
/* Returns the highest used index in subject (or issuer) DN of
 * certificate CERT for OID, or -1 if no RDNs are present in the DN
 * using that OID. */
static int oid_find_highest_index(gnutls_x509_crt cert, int subject, const char *oid)
{
    int ret, idx = -1;

    UGLY_DEBUG();
    do {
        size_t len = 0;

        if (subject)
            ret = gnutls_x509_crt_get_dn_by_oid(cert, oid, ++idx, 0,
                                                NULL, &len);
        else
            ret = gnutls_x509_crt_get_issuer_dn_by_oid(cert, oid, ++idx, 0,
                                                       NULL, &len);
    } while (ret == GNUTLS_E_SHORT_MEMORY_BUFFER);

    return idx - 1;
}

#ifdef HAVE_GNUTLS_X509_DN_GET_RDN_AVA
/* New-style RDN handling introduced in GnuTLS 1.7.x. */

#ifdef HAVE_ICONV
static void convert_dirstring(nussl_buffer *buf, const char *charset,
                              gnutls_datum *data)
{
    iconv_t id = iconv_open("UTF-8", charset);
    size_t inlen = data->size, outlen = buf->length - buf->used;
    char *inbuf = (char *)data->data;
    char *outbuf = buf->data + buf->used - 1;

    UGLY_DEBUG();
    if (id == (iconv_t)-1) {
        char err[128], err2[128];

        nussl_snprintf(err, sizeof err, "[unprintable in %s: %s]",
                    charset, nussl_strerror(errno, err2, sizeof err2));
        nussl_buffer_zappend(buf, err);
        return;
    }

    nussl_buffer_grow(buf, buf->used + 64);

    while (inlen && outlen
           && iconv(id, &inbuf, &inlen, &outbuf, &outlen) == 0)
        ;

    iconv_close(id);
    buf->used += buf->length - buf->used - outlen;
    buf->data[buf->used - 1] = '\0';
}
#endif

/* From section 11.13 of the Dubuisson ASN.1 bible: */
#define TAG_UTF8 (12)
#define TAG_PRINTABLE (19)
#define TAG_T61 (20)
#define TAG_IA5 (22)
#define TAG_VISIBLE (26)
#define TAG_UNIVERSAL (28)
#define TAG_BMP (30)

static void append_dirstring(nussl_buffer *buf, gnutls_datum *data, unsigned long tag)
{
    UGLY_DEBUG();
    switch (tag) {
    case TAG_UTF8:
    case TAG_IA5:
    case TAG_PRINTABLE:
    case TAG_VISIBLE:
        nussl_buffer_append(buf, (char *)data->data, data->size);
        break;
#ifdef HAVE_ICONV
    case TAG_T61:
        convert_dirstring(buf, "ISO-8859-1", data);
        break;
    case TAG_BMP:
        convert_dirstring(buf, "UCS-2BE", data);
        break;
#endif
    default: {
        char tmp[128];
        nussl_snprintf(tmp, sizeof tmp, _("[unprintable:#%lu]"), tag);
        nussl_buffer_zappend(buf, tmp);
    } break;
    }
}

/* OIDs to not include in readable DNs by default: */
#define OID_emailAddress "1.2.840.113549.1.9.1"
#define OID_commonName "2.5.4.3"

#define CMPOID(a,o) ((a)->oid.size == sizeof(o)                        \
                     && memcmp((a)->oid.data, o, strlen(o)) == 0)

char *nussl_ssl_readable_dname(const nussl_ssl_dname *name)
{
    gnutls_x509_dn_t dn;
    int ret, rdn = 0, flag = 0;
    nussl_buffer *buf;
    gnutls_x509_ava_st val;

    UGLY_DEBUG();
    if (name->subject)
        ret = gnutls_x509_crt_get_subject(name->cert, &dn);
    else
        ret = gnutls_x509_crt_get_issuer(name->cert, &dn);

    if (ret)
        return nussl_strdup(_("[unprintable]"));

    buf = nussl_buffer_create();

    /* Find the highest rdn... */
    while (gnutls_x509_dn_get_rdn_ava(dn, rdn++, 0, &val) == 0)
        ;

    /* ..then iterate back to the first: */
    while (--rdn >= 0) {
        int ava = 0;

        /* Iterate through all AVAs for multivalued AVAs; better than
         * nussl_openssl can do! */
        do {
            ret = gnutls_x509_dn_get_rdn_ava(dn, rdn, ava, &val);

            /* If the *only* attribute to append is the common name or
             * email address, use it; otherwise skip those
             * attributes. */
            if (ret == 0 && val.value.size > 0
                && ((!CMPOID(&val, OID_emailAddress)
                     && !CMPOID(&val, OID_commonName))
                    || (buf->used == 1 && rdn == 0))) {
                flag = 1;
                if (buf->used > 1) nussl_buffer_append(buf, ", ", 2);

                append_dirstring(buf, &val.value, val.value_tag);
            }

            ava++;
        } while (ret == 0);
    }

    return nussl_buffer_finish(buf);
}

#else /* !HAVE_GNUTLS_X509_DN_GET_RDN_AVA */

/* Appends the value of RDN with given oid from certitifcate x5
 * subject (if subject is non-zero), or issuer DN to buffer 'buf': */
static void append_rdn(nussl_buffer *buf, gnutls_x509_crt x5, int subject, const char *oid)
{
    int idx, top, ret;
    char rdn[50];

    UGLY_DEBUG();
    top = oid_find_highest_index(x5, subject, oid);

    for (idx = top; idx >= 0; idx--) {
        size_t rdnlen = sizeof rdn;

        if (subject)
            ret = gnutls_x509_crt_get_dn_by_oid(x5, oid, idx, 0, rdn, &rdnlen);
        else
            ret = gnutls_x509_crt_get_issuer_dn_by_oid(x5, oid, idx, 0, rdn, &rdnlen);

        if (ret < 0)
            return;

        if (buf->used > 1) {
            nussl_buffer_append(buf, ", ", 2);
        }

        nussl_buffer_append(buf, rdn, rdnlen);
    }
}

char *nussl_ssl_readable_dname(const nussl_ssl_dname *name)
{
    nussl_buffer *buf = nussl_buffer_create();
    int ret, idx = 0;

    UGLY_DEBUG();
    do {
        char oid[32] = {0};
        size_t oidlen = sizeof oid;

        ret = name->subject
            ? gnutls_x509_crt_get_dn_oid(name->cert, idx, oid, &oidlen)
            : gnutls_x509_crt_get_issuer_dn_oid(name->cert, idx, oid, &oidlen);

        if (ret == 0) {
            append_rdn(buf, name->cert, name->subject, oid);
            idx++;
        }
    } while (ret != GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE);

    return nussl_buffer_finish(buf);
}
#endif /* HAVE_GNUTLS_X509_DN_GET_RDN_AVA */

int nussl_ssl_dname_cmp(const nussl_ssl_dname *dn1, const nussl_ssl_dname *dn2)
{
    char c1[1024], c2[1024];
    size_t s1 = sizeof c1, s2 = sizeof c2;
    int ret;

    UGLY_DEBUG();
    if (dn1->subject)
        ret = gnutls_x509_crt_get_dn(dn1->cert, c1, &s1);
    else
        ret = gnutls_x509_crt_get_issuer_dn(dn1->cert, c1, &s1);
    if (ret)
        return 1;

    if (dn2->subject)
        ret = gnutls_x509_crt_get_dn(dn2->cert, c2, &s2);
    else
        ret = gnutls_x509_crt_get_issuer_dn(dn2->cert, c2, &s2);
    if (ret)
        return -1;

    if (s1 != s2)
        return s2 - s1;

    return memcmp(c1, c2, s1);
}

void nussl_ssl_clicert_free(nussl_ssl_client_cert *cc)
{
    UGLY_DEBUG();
    if (cc->p12)
        gnutls_pkcs12_deinit(cc->p12);
    if (cc->decrypted) {
        if (cc->cert.identity) nussl_free(cc->cert.identity);
        if (cc->pkey) gnutls_x509_privkey_deinit(cc->pkey);
        if (cc->cert.subject) gnutls_x509_crt_deinit(cc->cert.subject);
    }
    if (cc->friendly_name) nussl_free(cc->friendly_name);
    nussl_free(cc);
}

void nussl_ssl_cert_validity_time(const nussl_ssl_certificate *cert,
                               time_t *from, time_t *until)
{
    UGLY_DEBUG();
    if (from) {
        *from = gnutls_x509_crt_get_activation_time(cert->subject);
    }
    if (until) {
        *until = gnutls_x509_crt_get_expiration_time(cert->subject);
    }
}

/* Return non-zero if hostname from certificate (cn) matches hostname
 * used for session (hostname).  (Wildcard matching is no longer
 * mandated by RFC3280, but certs are deployed which use wildcards) */
static int match_hostname(char *cn, const char *hostname)
{
    const char *dot;
    NUSSL_DEBUG(NUSSL_DBG_SSL, "Match %s on %s...\n", cn, hostname);
    UGLY_DEBUG();
    dot = strchr(hostname, '.');
    if (dot == NULL) {
	char *pnt = strchr(cn, '.');
	/* hostname is not fully-qualified; unqualify the cn. */
	if (pnt != NULL) {
	    *pnt = '\0';
	}
    }
    else if (strncmp(cn, "*.", 2) == 0) {
	hostname = dot + 1;
	cn += 2;
    }
    return !nussl_strcasecmp(cn, hostname);
}

/* Check certificate identity.  Returns zero if identity matches; 1 if
 * identity does not match, or <0 if the certificate had no identity.
 * If 'identity' is non-NULL, store the malloc-allocated identity in
 * *identity.  If 'server' is non-NULL, it must be the network address
 * of the server in use, and identity must be NULL. */
/* static int check_identity(const nussl_uri *server, gnutls_x509_crt cert, */
/*                           char **identity) */
static int check_identity(gnutls_x509_crt cert,
                          char **identity)
{
    char name[255];
    unsigned int critical;
    int ret, seq = 0;
    int match = 0, found = 0;
    size_t len;
    const char *hostname;

    return 0;
    hostname = /*server ? server->host :*/ "";

    UGLY_DEBUG();
    do {
        len = sizeof name;
        ret = gnutls_x509_crt_get_subject_alt_name(cert, seq, name, &len,
                                                   &critical);
        switch (ret) {
        case GNUTLS_SAN_DNSNAME:
            if (identity && !found) *identity = nussl_strdup(name);
            match = match_hostname(name, hostname);
            found = 1;
            break;
        case GNUTLS_SAN_IPADDRESS: {
            nussl_inet_addr *ia;
            if (len == 4)
                ia = nussl_iaddr_make(nussl_iaddr_ipv4, (unsigned char *)name);
            else if (len == 16)
                ia = nussl_iaddr_make(nussl_iaddr_ipv6, (unsigned char *)name);
            else
                ia = NULL;
            if (ia) {
                char buf[128];

                match = strcmp(hostname,
                               nussl_iaddr_print(ia, buf, sizeof buf)) == 0;
                if (identity) *identity = nussl_strdup(buf);
                found = 1;
                nussl_iaddr_free(ia);
            } else {
		    /* XXX: NUSSL_FMT_SIZE_T n'est pas trouve et est dans config.h mais avec un #ifdef WIN32 */
/*                 NUSSL_DEBUG(NUSSL_DBG_SSL, "iPAddress name with unsupported " */
/*                          "address type (length %" NUSSL_FMT_SIZE_T "), skipped.\n", */
/*                          len); */
            }
        } break;
/*         case GNUTLS_SAN_URI: { */
/*             nussl_uri uri; */

/*             if (nussl_uri_parse(name, &uri) == 0 && uri.host && uri.scheme) { */
/*                 nussl_uri tmp; */

/*                 if (identity && !found) *identity = nussl_strdup(name); */
/*                 found = 1; */

/*                 if (server) { */
/*                     /\* For comparison purposes, all that matters is */
/*                      * host, scheme and port; ignore the rest. *\/ */
/*                     memset(&tmp, 0, sizeof tmp); */
/*                     tmp.host = uri.host; */
/*                     tmp.scheme = uri.scheme; */
/*                     tmp.port = uri.port; */

/*                     match = nussl_uri_cmp(server, &tmp) == 0; */
/*                 } */
/*             } */

/*             nussl_uri_free(&uri); */
/*         } break; */

        default:
            break;
        }
        seq++;
    } while (!match && ret != GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE);

    /* Check against the commonName if no DNS alt. names were found,
     * as per RFC3280. */
    if (!found) {
        seq = oid_find_highest_index(cert, 1, GNUTLS_OID_X520_COMMON_NAME);

        if (seq >= 0) {
            len = sizeof name;
            name[0] = '\0';
            ret = gnutls_x509_crt_get_dn_by_oid(cert, GNUTLS_OID_X520_COMMON_NAME,
                                                seq, 0, name, &len);
            if (ret == 0) {
                if (identity) *identity = nussl_strdup(name);
                match = match_hostname(name, hostname);
            }
        } else {
            return -1;
        }
    }

    NUSSL_DEBUG(NUSSL_DBG_SSL, "Identity match: %s\n", match ? "good" : "bad");
    return match ? 0 : 1;
}

/* Populate an nussl_ssl_certificate structure from an X509 object. */
static nussl_ssl_certificate *populate_cert(nussl_ssl_certificate *cert,
                                         gnutls_x509_crt x5)
{
    UGLY_DEBUG();
    cert->subj_dn.cert = x5;
    cert->subj_dn.subject = 1;
    cert->issuer_dn.cert = x5;
    cert->issuer_dn.subject = 0;
    cert->issuer = NULL;
    cert->subject = x5;
    cert->identity = NULL;
/*     check_identity(NULL, x5, &cert->identity); */
    check_identity(x5, &cert->identity); /* TODO: return the error of check_identity ... */
    return cert;
}

/* Returns a copy certificate of certificate SRC. */
static gnutls_x509_crt x509_crt_copy(gnutls_x509_crt src)
{
    int ret;
    size_t size = 0;
    gnutls_datum tmp;
    gnutls_x509_crt dest;

    UGLY_DEBUG();
    if (gnutls_x509_crt_init(&dest) != 0) {
        return NULL;
    }

    if (gnutls_x509_crt_export(src, GNUTLS_X509_FMT_DER, NULL, &size)
        != GNUTLS_E_SHORT_MEMORY_BUFFER) {
        gnutls_x509_crt_deinit(dest);
        return NULL;
    }

    tmp.data = nussl_malloc(size);
    ret = gnutls_x509_crt_export(src, GNUTLS_X509_FMT_DER, tmp.data, &size);
    if (ret == 0) {
        tmp.size = size;
        ret = gnutls_x509_crt_import(dest, &tmp, GNUTLS_X509_FMT_DER);
    }

    if (ret) {
        gnutls_x509_crt_deinit(dest);
        dest = NULL;
    }

    nussl_free(tmp.data);
    return dest;
}

/* Duplicate a client certificate, which must be in the decrypted state. */
static nussl_ssl_client_cert *dup_client_cert(const nussl_ssl_client_cert *cc)
{
    int ret;
    nussl_ssl_client_cert *newcc = nussl_calloc(sizeof *newcc);

    UGLY_DEBUG();
    newcc->decrypted = 1;

    ret = gnutls_x509_privkey_init(&newcc->pkey);
    if (ret != 0) goto dup_error;

    ret = gnutls_x509_privkey_cpy(newcc->pkey, cc->pkey);
    if (ret != 0) goto dup_error;

    newcc->cert.subject = x509_crt_copy(cc->cert.subject);
    if (!newcc->cert.subject) goto dup_error;

    if (cc->friendly_name) newcc->friendly_name = nussl_strdup(cc->friendly_name);

    populate_cert(&newcc->cert, newcc->cert.subject);
    return newcc;

dup_error:
    if (newcc->pkey) gnutls_x509_privkey_deinit(newcc->pkey);
    if (newcc->cert.subject) gnutls_x509_crt_deinit(newcc->cert.subject);
    nussl_free(newcc);
    return NULL;
}

#if 0 /* Use gnutls function, no callback needed */
/* Callback invoked when the SSL server requests a client certificate.  */
static int provide_client_cert(gnutls_session session,
                               const gnutls_datum *req_ca_rdn, int nreqs,
                               const gnutls_pk_algorithm *sign_algos,
                               int sign_algos_length, gnutls_retr_st *st)
{
    nussl_session *sess = gnutls_session_get_ptr(session);
    UGLY_DEBUG();

    if (!sess) {
        return -1;
    }

    if (!sess->client_cert && sess->ssl_provide_fn) {
        /* The dname array cannot be converted without better dname
         * support from GNUTLS. */
        sess->ssl_provide_fn(sess->ssl_provide_ud, sess,
                             NULL, 0);
    }

    NUSSL_DEBUG(NUSSL_DBG_SSL, "In client cert provider callback.\n");

    if (sess->client_cert) {
        gnutls_certificate_type type = gnutls_certificate_type_get(session);
        if (type == GNUTLS_CRT_X509) {
            NUSSL_DEBUG(NUSSL_DBG_SSL, "Supplying client certificate.\n");

            st->type = type;
            st->ncerts = 1;
            st->cert.x509 = &sess->client_cert->cert.subject;
            st->key.x509 = sess->client_cert->pkey;

            /* tell GNU TLS not to deallocate the certs. */
            st->deinit_all = 0;
        } else {
            return -1;
        }
    } else {
        NUSSL_DEBUG(NUSSL_DBG_SSL, "No client certificate supplied.\n");
    }

    return 0;
}
#endif

nussl_ssl_context *nussl_ssl_context_create(int flags)
{
    nussl_ssl_context *ctx = nussl_calloc(sizeof *ctx);
    UGLY_DEBUG();
    gnutls_certificate_allocate_credentials(&ctx->cred);
/*    if (flags == NUSSL_SSL_CTX_CLIENT) {
        gnutls_certificate_client_set_retrieve_function(ctx->cred,
                                                        provide_client_cert);
    }*/
    return ctx;
}

#if 0
int nussl_ssl_context_keypair(nussl_ssl_context *ctx,
                           const char *cert, const char *key)
{
    UGLY_DEBUG();
    return (gnutls_certificate_set_x509_key_file(ctx->cred, cert, key,
                                         GNUTLS_X509_FMT_PEM) == 0) ? NUSSL_OK : NUSSL_ERROR;
}
#endif

int nussl_ssl_context_keypair_from_data(nussl_ssl_context *ctx, nussl_ssl_client_cert* cert)
{
    UGLY_DEBUG();
    int ret;
    ret = gnutls_certificate_set_x509_key(ctx->cred, &cert->cert.subject, 1, cert->pkey);
    return (ret == 0) ? NUSSL_OK : NUSSL_ERROR;
}


int nussl_ssl_set_clicert(nussl_session *sess, const nussl_ssl_client_cert *cc)
{
    UGLY_DEBUG();
    sess->client_cert = dup_client_cert(cc);
    if (!sess->client_cert)
    	return NUSSL_ERROR;

    return nussl_ssl_context_keypair_from_data(sess->ssl_context, sess->client_cert);
}

#if 0
int nussl_ssl_context_set_verify(nussl_ssl_context *ctx, int required,
                              const char *ca_names, const char *verify_cas)
{
    UGLY_DEBUG();
    ctx->verify = required;
    if (verify_cas) {
        gnutls_certificate_set_x509_trust_file(ctx->cred, verify_cas,
                                               GNUTLS_X509_FMT_PEM);
    }
    /* gnutls_certificate_send_x509_rdn_sequence in gnutls >= 1.2 can
     * be used to *suppress* sending the CA names, but not control it,
     * it seems. */
    return 0;
}
#endif

void nussl_ssl_context_set_flag(nussl_ssl_context *ctx, int flag, int value)
{
    /* SSLv2 not supported. */
}

void nussl_ssl_context_destroy(nussl_ssl_context *ctx)
{
    UGLY_DEBUG();
    gnutls_certificate_free_credentials(ctx->cred);
    if (ctx->cache.client.data) {
        nussl_free(ctx->cache.client.data);
    } else if (ctx->cache.server.key.data) {
        gnutls_free(ctx->cache.server.key.data);
        gnutls_free(ctx->cache.server.data.data);
    }
    nussl_free(ctx);
}

/* Return the certificate chain sent by the peer, or NULL on error. */
static nussl_ssl_certificate *make_peers_chain(gnutls_session sock)
{
    nussl_ssl_certificate *current = NULL, *top = NULL;
    const gnutls_datum *certs;
    unsigned int n, count;

    UGLY_DEBUG();
    certs = gnutls_certificate_get_peers(sock, &count);
    if (!certs) {
        return NULL;
    }

    for (n = 0; n < count; n++) {
        nussl_ssl_certificate *cert;
        gnutls_x509_crt x5;

        if (gnutls_x509_crt_init(&x5) ||
            gnutls_x509_crt_import(x5, &certs[n], GNUTLS_X509_FMT_DER)) {
            nussl_ssl_cert_free(top);
            return NULL;
        }

        cert = populate_cert(nussl_malloc(sizeof *cert), x5);

        if (top == NULL) {
            current = top = cert;
        } else {
            current->issuer = cert;
            current = cert;
        }
    }

    return top;
}

/* Verifies an SSL server certificate. */
static int check_certificate(nussl_session *sess, gnutls_session sock,
                             nussl_ssl_certificate *chain)
{
    time_t before, after, now = time(NULL);
    int ret, failures = 0;
    unsigned int status;
/*     nussl_uri server; */

    UGLY_DEBUG();
    before = gnutls_x509_crt_get_activation_time(chain->subject);
    after = gnutls_x509_crt_get_expiration_time(chain->subject);

    if (now < before)
        failures |= NUSSL_SSL_NOTYETVALID;
    else if (now > after)
        failures |= NUSSL_SSL_EXPIRED;

/*     memset(&server, 0, sizeof server); */
/*     nussl_fill_server_uri(sess, &server); */
/*     ret = check_identity(&server, chain->subject, NULL); */
    ret = check_identity(chain->subject, NULL);
/*     nussl_uri_free(&server); */

    if (ret < 0) {
        nussl_set_error(sess, _("Server certificate was missing commonName "
                             "attribute in subject name"));
        return NUSSL_ERROR;
    } else if (ret > 0) {
        failures |= NUSSL_SSL_IDMISMATCH;
    }

    ret = gnutls_certificate_verify_peers2(sock, &status);
    if (ret < 0) {
                    printf("Certificate authority verification failed: %s\n",
                           gnutls_strerror(ret));
        failures |= NUSSL_SSL_UNTRUSTED;
    }
    if (status) {
                    printf("Certificate authority verification failed:");
                    if( status & GNUTLS_CERT_INVALID )
                            printf(" invalid");
                    if( status & GNUTLS_CERT_REVOKED )
                            printf(", revoked");
                    if( status & GNUTLS_CERT_SIGNER_NOT_FOUND )
                            printf(", signer not found");
                    if( status & GNUTLS_CERT_SIGNER_NOT_CA )
                            printf(", signer not a CA");
                    printf("\n");
        failures |= NUSSL_SSL_UNTRUSTED;
	    }


    NUSSL_DEBUG(NUSSL_DBG_SSL, "Failures = %d\n", failures);

    if (failures == 0) {
        ret = NUSSL_OK;
    } else {
        nussl__ssl_set_verify_err(sess, failures);
        ret = NUSSL_ERROR;
#if 0
        if (sess->ssl_verify_fn
            && sess->ssl_verify_fn(sess->ssl_verify_ud, failures, chain) == 0)
            ret = NUSSL_OK;
#endif
    }
    return ret;
}

/* Negotiate an SSL connection. */
int nussl__negotiate_ssl(nussl_session *sess)
{
    nussl_ssl_context *const ctx = sess->ssl_context;
    nussl_ssl_certificate *chain;
    gnutls_session sock;

    UGLY_DEBUG();
    NUSSL_DEBUG(NUSSL_DBG_SSL, "Negotiating SSL connection.\n");

    /* Pass through the hostname if SNI is enabled. */
    ctx->hostname =
        sess->flags[NUSSL_SESSFLAG_TLS_SNI] ? sess->server.hostname : NULL;

    if (nussl_sock_connect_ssl(sess->socket, ctx, sess)) {
	nussl_set_error(sess, _("SSL negotiation failed: %s"),
		     nussl_sock_error(sess->socket));
	return NUSSL_ERROR;
    }

    sock = nussl__sock_sslsock(sess->socket);

    if (!sess->check_peer_cert)
        return NUSSL_OK;

    chain = make_peers_chain(sock);
    if (chain == NULL) {
        nussl_set_error(sess, _("Server did not send certificate chain"));
        return NUSSL_ERROR;
    }

    if (sess->server_cert && nussl_ssl_cert_cmp(sess->server_cert, chain) == 0) {
        /* Same cert as last time; presume OK.  This is not optimal as
         * make_peers_chain() has already gone through and done the
         * expensive DER parsing stuff for the whole chain by now. */
        nussl_ssl_cert_free(chain);
        return NUSSL_OK;
    }

    if (check_certificate(sess, sock, chain)) {
        nussl_ssl_cert_free(chain);
        return NUSSL_ERROR;
    }

    sess->server_cert = chain;
    return NUSSL_OK;
}

const nussl_ssl_dname *nussl_ssl_cert_issuer(const nussl_ssl_certificate *cert)
{
    UGLY_DEBUG();
    return &cert->issuer_dn;
}

const nussl_ssl_dname *nussl_ssl_cert_subject(const nussl_ssl_certificate *cert)
{
    UGLY_DEBUG();
    return &cert->subj_dn;
}

const nussl_ssl_certificate *nussl_ssl_cert_signedby(const nussl_ssl_certificate *cert)
{
    UGLY_DEBUG();
    return cert->issuer;
}

const char *nussl_ssl_cert_identity(const nussl_ssl_certificate *cert)
{
    UGLY_DEBUG();
    return cert->identity;
}

int nussl_ssl_context_trustcert(nussl_ssl_context *ctx, const nussl_ssl_certificate *cert)
{
    UGLY_DEBUG();
    gnutls_x509_crt certs = cert->subject;
    return (gnutls_certificate_set_x509_trust(ctx->cred, &certs, 1) == 0) ? NUSSL_OK : NUSSL_ERROR;
}

void nussl_ssl_trust_default_ca(nussl_session *sess)
{
#ifdef NUSSL_SSL_CA_BUNDLE
    UGLY_DEBUG();
    gnutls_certificate_set_x509_trust_file(sess->ssl_context->cred,
                                           NUSSL_SSL_CA_BUNDLE,
                                           GNUTLS_X509_FMT_PEM);
#endif
}

/* Read the contents of file FILENAME into *DATUM. */
static int read_to_datum(const char *filename, gnutls_datum *datum)
{
    FILE *f = fopen(filename, "r");
    nussl_buffer *buf;
    char tmp[4192];
    size_t len;

    UGLY_DEBUG();
    if (!f) {
        return -1;
    }

    buf = nussl_buffer_ncreate(8192);
    while ((len = fread(tmp, 1, sizeof tmp, f)) > 0) {
        nussl_buffer_append(buf, tmp, len);
    }

    if (!feof(f)) {
        nussl_buffer_destroy(buf);
        return -1;
    }

    datum->size = nussl_buffer_size(buf);
    datum->data = (unsigned char *)nussl_buffer_finish(buf);
    return 0;
}

/* Parses a PKCS#12 structure and loads the certificate, private key
 * and friendly name if possible.  Returns zero on success, non-zero
 * on error. */
static int pkcs12_parse(gnutls_pkcs12 p12, gnutls_x509_privkey *pkey,
                        gnutls_x509_crt *x5, char **friendly_name,
                        const char *password)
{
    gnutls_pkcs12_bag bag = NULL;
    int i, j, ret = 0;

    UGLY_DEBUG();
    for (i = 0; ret == 0; ++i) {
        if (bag) gnutls_pkcs12_bag_deinit(bag);

        ret = gnutls_pkcs12_bag_init(&bag);
        if (ret < 0) continue;

        ret = gnutls_pkcs12_get_bag(p12, i, bag);
        if (ret < 0) continue;

        gnutls_pkcs12_bag_decrypt(bag, password);

        for (j = 0; ret == 0 && j < gnutls_pkcs12_bag_get_count(bag); ++j) {
            gnutls_pkcs12_bag_type type;
            gnutls_datum data;

            if (friendly_name && *friendly_name == NULL) {
                char *name = NULL;
                gnutls_pkcs12_bag_get_friendly_name(bag, j, &name);
                if (name) {
                    if (name[0] == '.') name++; /* weird GnuTLS bug? */
                    *friendly_name = nussl_strdup(name);
                }
            }

            type = gnutls_pkcs12_bag_get_type(bag, j);
            switch (type) {
            case GNUTLS_BAG_PKCS8_KEY:
            case GNUTLS_BAG_PKCS8_ENCRYPTED_KEY:
                gnutls_x509_privkey_init(pkey);

                ret = gnutls_pkcs12_bag_get_data(bag, j, &data);
                if (ret < 0) continue;

                ret = gnutls_x509_privkey_import_pkcs8(*pkey, &data,
                                                       GNUTLS_X509_FMT_DER,
                                                       password,
                                                       0);
                if (ret < 0) continue;
                break;
            case GNUTLS_BAG_CERTIFICATE:
                gnutls_x509_crt_init(x5);

                ret = gnutls_pkcs12_bag_get_data(bag, j, &data);
                if (ret < 0) continue;

                ret = gnutls_x509_crt_import(*x5, &data, GNUTLS_X509_FMT_DER);
                if (ret < 0) continue;

                break;
            default:
                break;
            }
        }
    }

    /* Make sure last bag is freed */
    if (bag) gnutls_pkcs12_bag_deinit(bag);

    /* Free in case of error */
    if (ret < 0 && ret != GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE) {
        if (*x5) gnutls_x509_crt_deinit(*x5);
        if (*pkey) gnutls_x509_privkey_deinit(*pkey);
        if (friendly_name && *friendly_name) nussl_free(*friendly_name);
    }

    if (ret == GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE) ret = 0;
    return ret;
}

nussl_ssl_client_cert *nussl_ssl_clicert_read(const char *filename)
{
    int ret;
    gnutls_datum data;
    gnutls_pkcs12 p12;
    nussl_ssl_client_cert *cc;
    char *friendly_name = NULL;
    gnutls_x509_crt cert = NULL;
    gnutls_x509_privkey pkey = NULL;

    UGLY_DEBUG();
    if (read_to_datum(filename, &data))
        return NULL;

    if (gnutls_pkcs12_init(&p12) != 0) {
        return NULL;
    }

    ret = gnutls_pkcs12_import(p12, &data, GNUTLS_X509_FMT_DER, 0);
    nussl_free(data.data);
    if (ret < 0) {
        gnutls_pkcs12_deinit(p12);
        return NULL;
    }

    if (gnutls_pkcs12_verify_mac(p12, "") == 0) {
        if (pkcs12_parse(p12, &pkey, &cert, &friendly_name, "") != 0) {
            gnutls_pkcs12_deinit(p12);
            return NULL;
        }

        cc = nussl_calloc(sizeof *cc);
        cc->pkey = pkey;
        cc->decrypted = 1;
        cc->friendly_name = friendly_name;
        populate_cert(&cc->cert, cert);
        gnutls_pkcs12_deinit(p12);
        cc->p12 = NULL;
        return cc;
    } else {
        /* TODO: calling pkcs12_parse() here to find the friendly_name
         * seems to break horribly.  */
        cc = nussl_calloc(sizeof *cc);
        cc->p12 = p12;
        return cc;
    }
}

int nussl_ssl_clicert_encrypted(const nussl_ssl_client_cert *cc)
{
    UGLY_DEBUG();
    return !cc->decrypted;
}

int nussl_ssl_clicert_decrypt(nussl_ssl_client_cert *cc, const char *password)
{
    int ret;
    gnutls_x509_crt cert = NULL;
    gnutls_x509_privkey pkey = NULL;

    UGLY_DEBUG();
    if (gnutls_pkcs12_verify_mac(cc->p12, password) != 0) {
        return -1;
    }

    ret = pkcs12_parse(cc->p12, &pkey, &cert, NULL, password);
    if (ret < 0)
        return ret;

    gnutls_pkcs12_deinit(cc->p12);
    populate_cert(&cc->cert, cert);
    cc->pkey = pkey;
    cc->decrypted = 1;
    cc->p12 = NULL;
    return 0;
}

const nussl_ssl_certificate *nussl_ssl_clicert_owner(const nussl_ssl_client_cert *cc)
{
    UGLY_DEBUG();
    return &cc->cert;
}

const char *nussl_ssl_clicert_name(const nussl_ssl_client_cert *ccert)
{
    UGLY_DEBUG();
    return ccert->friendly_name;
}

nussl_ssl_certificate *nussl_ssl_cert_read(const char *filename)
{
    int ret;
    gnutls_datum data;
    gnutls_x509_crt x5;

    UGLY_DEBUG();
    if (read_to_datum(filename, &data))
        return NULL;

    if (gnutls_x509_crt_init(&x5) != 0)
        return NULL;

    ret = gnutls_x509_crt_import(x5, &data, GNUTLS_X509_FMT_PEM);
    nussl_free(data.data);
    if (ret < 0) {
    	printf("Unable to load cert..\n");
        gnutls_x509_crt_deinit(x5);
        return NULL;
    }

    return populate_cert(nussl_calloc(sizeof(struct nussl_ssl_certificate_s)), x5);
}

int nussl_ssl_cert_write(const nussl_ssl_certificate *cert, const char *filename)
{
    unsigned char buffer[10*1024];
    size_t len = sizeof buffer;

    FILE *fp = fopen(filename, "w");

    UGLY_DEBUG();
    if (fp == NULL) return -1;

    if (gnutls_x509_crt_export(cert->subject, GNUTLS_X509_FMT_PEM, buffer,
                               &len) < 0) {
        fclose(fp);
        return -1;
    }

    if (fwrite(buffer, len, 1, fp) != 1) {
        fclose(fp);
        return -1;
    }

    if (fclose(fp) != 0)
        return -1;

    return 0;
}

void nussl_ssl_cert_free(nussl_ssl_certificate *cert)
{
    UGLY_DEBUG();
    gnutls_x509_crt_deinit(cert->subject);
    if (cert->identity) nussl_free(cert->identity);
    if (cert->issuer) nussl_ssl_cert_free(cert->issuer);
    nussl_free(cert);
}

int nussl_ssl_cert_cmp(const nussl_ssl_certificate *c1, const nussl_ssl_certificate *c2)
{
    char digest1[NUSSL_SSL_DIGESTLEN], digest2[NUSSL_SSL_DIGESTLEN];

    UGLY_DEBUG();
    if (nussl_ssl_cert_digest(c1, digest1) || nussl_ssl_cert_digest(c2, digest2)) {
        return -1;
    }

    return strcmp(digest1, digest2);
}

nussl_ssl_client_cert* nussl_ssl_import_keypair(nussl_session* sess,
                           const char *cert_file, const char *key_file)
{
	nussl_ssl_client_cert* keypair = NULL;
	gnutls_datum cert_raw;
	gnutls_datum key_raw;

	keypair = nussl_calloc(sizeof(nussl_ssl_client_cert));
	if (keypair == NULL)
		return NULL;

	keypair->decrypted = 1;
	keypair->p12 = NULL;
	keypair->friendly_name = NULL;

	UGLY_DEBUG();

	if (gnutls_x509_crt_init(&keypair->cert.subject) < 0)
        	return NULL;
	if (read_to_datum(cert_file, &cert_raw) != NUSSL_OK)
        	return NULL;
	if (gnutls_x509_crt_import(keypair->cert.subject, &cert_raw, GNUTLS_X509_FMT_PEM) < 0)
		return NULL;

	if (populate_cert(&keypair->cert, keypair->cert.subject) == NULL)
        	return NULL;

	if (gnutls_x509_privkey_init(&keypair->pkey) < 0)
		return NULL;

	if (read_to_datum(key_file, &key_raw) != NUSSL_OK)
		return NULL;
	if (gnutls_x509_privkey_import(keypair->pkey, &key_raw, GNUTLS_X509_FMT_PEM) < 0)
		return NULL;

	return keypair;
}

/* The certificate import/export format is the base64 encoding of the
 * raw DER; PEM without the newlines and wrapping. */

nussl_ssl_certificate *nussl_ssl_cert_import(const char *data)
{
    int ret;
    size_t len;
    unsigned char *der;
    gnutls_datum buffer = { NULL, 0 };
    gnutls_x509_crt x5;

    UGLY_DEBUG();
    if (gnutls_x509_crt_init(&x5) != 0)
        return NULL;

    /* decode the base64 to get the raw DER representation */
    len = nussl_unbase64(data, &der);
    if (len == 0) return NULL;

    buffer.data = der;
    buffer.size = len;

    ret = gnutls_x509_crt_import(x5, &buffer, GNUTLS_X509_FMT_DER);
    nussl_free(der);

    if (ret < 0) {
        gnutls_x509_crt_deinit(x5);
        return NULL;
    }

    return populate_cert(nussl_calloc(sizeof(struct nussl_ssl_certificate_s)), x5);
}

char *nussl_ssl_cert_export(const nussl_ssl_certificate *cert)
{
    unsigned char *der;
    size_t len = 0;
    char *ret;

    UGLY_DEBUG();
    /* find the length of the DER encoding. */
    if (gnutls_x509_crt_export(cert->subject, GNUTLS_X509_FMT_DER, NULL, &len) !=
        GNUTLS_E_SHORT_MEMORY_BUFFER) {
        return NULL;
    }

    der = nussl_malloc(len);
    if (gnutls_x509_crt_export(cert->subject, GNUTLS_X509_FMT_DER, der, &len)) {
        nussl_free(der);
        return NULL;
    }

    ret = nussl_base64(der, len);
    nussl_free(der);
    return ret;
}

int nussl_ssl_cert_digest(const nussl_ssl_certificate *cert, char *digest)
{
    char sha1[20], *p;
    int j;
    size_t len = sizeof sha1;

    UGLY_DEBUG();
    if (gnutls_x509_crt_get_fingerprint(cert->subject, GNUTLS_DIG_SHA,
                                        sha1, &len) < 0)
        return -1;

    for (j = 0, p = digest; j < 20; j++) {
        *p++ = NUSSL_HEX2ASC((sha1[j] >> 4) & 0x0f);
        *p++ = NUSSL_HEX2ASC(sha1[j] & 0x0f);
        *p++ = ':';
    }

    *--p = '\0';
    return 0;
}

/* Begin: --INL-- DH management functions added */
/**
 * Generate Diffie Hellman parameters - for use with DHE
 * (Ephemeral Diffie Hellman) kx algorithms. These should be discarded
 * and regenerated once a day, once a week or once a month. Depending on
 * the security requirements.
 *
 * \return If an error occurs returns -1, else return 0
 */
int nussl_ssl_cert_generate_dh_params(nussl_ssl_context *ctx)
{
        if (gnutls_dh_params_init(ctx->dh) < 0) {
                return -1;
        }
        if (gnutls_dh_params_generate2(*ctx->dh_params, DH_BITS) < 0) {
                return -1;
        }

        return 0;
}

void nussl_ssl_cert_dh_params(nussl_ssl_context *ctx)
{

        gnutls_certificate_set_dh_params(ctx->cred, ctx->dh);

}


/* End: --INL-- */

int nussl__ssl_init(void)
{
    UGLY_DEBUG();
#ifdef NUSSL_HAVE_TS_SSL
    gcry_control(GCRYCTL_SET_THREAD_CBS, &gcry_threads_pthread);
#endif
    return gnutls_global_init();
}

void nussl__ssl_exit(void)
{
    UGLY_DEBUG();
    /* No way to unregister the thread callbacks.  Doomed. */
#if LIBGNUTLS_VERSION_MAJOR > 1 || LIBGNUTLS_VERSION_MINOR > 3 \
    || (LIBGNUTLS_VERSION_MINOR == 3 && LIBGNUTLS_VERSION_PATCH >= 3)
    /* It's safe to call gnutls_global_deinit() here only with
     * gnutls >= 1.3., since older versions don't refcount and
     * doing so would prevent any other use of gnutls within
     * the process. */
    gnutls_global_deinit();
#endif
}

#endif /* HAVE_GNUTLS */
