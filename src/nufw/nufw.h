#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <structure.h>
#include <nufw_debug.h>
#include <signal.h>

#include <strings.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_LIBIPQ_LIBIPQ_H 
#include <libipq/libipq.h>
#else
#ifdef HAVE_LIBIPQ_H
#include <libipq.h>
#else
#error "libipq needed for NuFW compilation"
#endif
#endif


#include <gnutls/gnutls.h>
#include <gcrypt.h>
#include <errno.h>

#define GRYZOR_HACKS
#undef GRYZOR_HACKS

#ifdef GRYZOR_HACKS
#include <sys/socket.h>
#endif

GCRY_THREAD_OPTION_PTHREAD_IMPL;





#define USE_X509 1
#define KEYFILE "/nufw-key.pem"
#define CERTFILE "/nufw-cert.pem"

struct nuauth_conn {
        gnutls_session * session;
        unsigned char active;
        int socket;
};

struct nuauth_conn tls;
int nufw_use_tls;
gnutls_session * tls_connect( );

/* socket number to send auth request */
int sck_auth_request;
struct sockaddr_in adr_srv, list_srv;

#ifdef GRYZOR_HACKS
//Raw socket we use for sending ICMP messages
int raw_sock;
#endif
/* 
 * all functions 
 */

// IP packet catcher

void* packetsrv();

// IP auth server

void* authsrv();

/* send an auth request packet given a payload (raw packet) */
int auth_request_send(u_int8_t type,unsigned long packet_id, char* payload,int data_len);
/* take decision given a auth answer packet payload */
int auth_packet_to_decision(char* dgram);


/* common */

unsigned long padd ( packet_idl * packet);
int psearch_and_destroy (unsigned long packet_id,unsigned long * mark);
int clean_old_packets ();

void process_usr1(int signum);
void process_usr2(int signum);
void process_poll(int signum);

#ifdef GRYZOR_HACKS
int send_icmp_unreach(char *dgram);
#endif
