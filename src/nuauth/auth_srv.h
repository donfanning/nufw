/* $Id: auth_srv.h,v 1.36 2004/06/17 22:36:11 regit Exp $ */

/*
** Copyright(C) 2003,2004 INL
** Written by Eric Leblond <regit@inl.fr>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, version 2 of the License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

/* Use glib to treat data structures */
#include <glib.h>
#include <gmodule.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>

/* SSL */
#include "../include/ssl.h"

/* config dependant */
#include <config.h>
/* NUFW Protocol */
#include <proto.h>

/*debug functions*/
#include <debug.h>

/* config file related */
#include <conffile.h>

/*
 * declare some global variables and do some definitions
 */

#define DUMMY 0
#define USE_LDAP 0
#define AUTHREQ_CLIENT_LISTEN_ADDR "0.0.0.0"
#define NUAUTH_DEFAULT_PROTOCOL 2
#define AUTHREQ_NUFW_LISTEN_ADDR "127.0.0.1"
#define GWSRV_ADDR "127.0.0.1"
//#define CLIENT_LISTEN_ADDR "0.0.0.0"
//#define NUFW_LISTEN_ADDR "127.0.0.1"
#define GWSRV_PORT 4128
#define AUTHREQ_PORT 4129
#define USERPCKT_PORT 4130
#define PRIO 1
#define PRIO_TO_NOK 1
#define HOSTNAME_SIZE 128
#define PACKET_TIMEOUT 15
#define DEFAULT_AUTH_MODULE "libldap"
//#define DEFAULT_LOGS_MODULE "libsyslog"
#define DEFAULT_LOGS_MODULE "libpgsql"
#define MODULE_PATH MODULE_DIR "/nuauth/modules/"
/* define the number of threads that will do user check */
#define NB_USERCHECK 10
/* define the number of threads that will check acls  */
#define NB_ACLCHECK 10

/* SSL stuffs */
#define NUAUTH_USE_SSL 0
#define NUAUTH_KEYFILE CONFIG_DIR "/nuauth.pem"
#define NUAUTH_KEY_PASSWD "password"
#define NUAUTH_SSL_MAX_CLIENTS 256

/* Start internal */

/* internal auth srv */
#define STATE_NONE 0x0
#define STATE_AUTHREQ 0x1
#define STATE_USERPCKT 0x2
#define STATE_READY 0x3
#define STATE_DONE 0x4

#define STATE_DROP 0x0
#define STATE_OPEN 0x1
#define STATE_ESTABLISHED 0x2
#define STATE_CLOSE 0x3

#define ALL_GROUPS 0

/* Sockets related */
char client_listen_address[HOSTNAME_SIZE];
char nufw_listen_address[HOSTNAME_SIZE];
int authreq_port;
int  gwsrv_port , userpckt_port;

typedef struct uniq_headers {
  /* IP */
  u_int32_t saddr;
  u_int32_t daddr;
  u_int8_t protocol;
  /* TCP or UDP */
  u_int16_t source;
  u_int16_t dest;
  /* ICMP Things */
  u_int8_t type;		/* message type */
  u_int8_t code;
} tracking;
//connection element


typedef struct Connection {
  // netfilter stuff
  GSList * packet_id; /* netfilter number */
  long timestamp;             /* Packet arrival time (seconds) */
  // IPV4  stuffs (headers)
  /* tracking test */
  tracking tracking_hdrs;
  u_int16_t id_srv;
  /* user params */
  u_int16_t user_id;
  char * username;
  /* generic list to stock acl related groups */
  GSList * acl_groups;
  // auth stuff 
  GSList * user_groups;
  /* state */
  char state;
  /* decision on packet */
  char decision;
} connection;


GSList * busy_mutex_list;
GSList * free_mutex_list;

/*
 * Keep connection in a List
 */


GHashTable * conn_list;
/* global lock for the conn list */
GStaticMutex insert_mutex;


GThreadPool* user_checkers;
GThreadPool* acl_checkers;
GThreadPool* user_loggers;
GThreadPool* decisions_workers;

GAsyncQueue* connexions_queue;

int packet_timeout;
int authpckt_port;
int debug; /* This will disapear*/
int debug_level;
int debug_areas;
int nuauth_log_users;
int nuauth_log_users_sync;
int nuauth_protocol_version;
int nuauth_prio_to_nok;
struct sockaddr_in adr_srv, client_srv, nufw_srv;

struct acl_group {
  GSList * groups;
  char answer;
};

GSList * ALLGROUP;

struct acl_group DUMMYACL ;
GSList * DUMMYACLS;


/*
 * user datas
 */

/* definition of user auth datas */

typedef struct _md5_user_auth_datas {
        char * password;
} md5_user_auth_datas;

typedef struct _user_auth_datas {
    u_int8_t type;
    md5_user_auth_datas * md5_datas;
} user_auth_datas;

typedef struct User_Datas {
	u_int32_t ip;
	long first_pckt_timestamp;
	long last_packet_time;
	unsigned long last_packet_id;
	long last_packet_timestamp;
        /* needed for auth cache */
        user_auth_datas *authdatas;
} user_datas;

GHashTable * users_hash;

/* internal for crypt */
GPrivate* crypt_priv;

/* internal for send_auth_response */

struct auth_answer {
  u_int8_t answer;
  u_int16_t user_id;
};

/* authentication structures */


typedef struct _md5_auth_field {
    char md5sum[34];
    long u_packet_id;
} md5_auth_field;


typedef struct _auth_field {
    u_int8_t type;
    /* a pointer on each type (md5 for now) */
    md5_auth_field * md5_datas;
    md5_user_auth_datas *  user_md5_datas;
} auth_field;




/*
 * Functions
 */

/*
 * From auth_common.c
 */

void search_and_fill ();

gboolean compare_connection(gconstpointer conn1, gconstpointer conn2);
int sck_auth_reply;
void send_auth_response(gpointer data, gpointer userdata);
int conn_cl_delete(gconstpointer conn);
char change_state(connection *elt, char state);
inline char get_state(connection *elt);
gint take_decision(connection * element);
gint print_connection(gpointer data,gpointer userdata);
int free_connection(connection * conn);
int lock_and_free_connection(connection * conn);
void clean_connections_list ();
guint hash_connection(gconstpointer conn_p);
void decisions_queue_work (gpointer userdata, gpointer data);
/*
 * From check_acls.c
 */

int external_acl_groups (connection * element);

/*
 * From pckt_authsrv.c
 */

void* packet_authsrv();
connection*  authpckt_decode(char * , int);
void acl_check_and_decide (gpointer userdata , gpointer data);

/*
 * From user_authsrv.c
 */

void* user_authsrv();
void* ssl_user_authsrv();
connection * userpckt_decode(char* dgram,int dgramsiz);
void user_check_and_decide (gpointer userdata ,gpointer data);
void * parse_packet_field(char* pointer, u_int8_t flag ,connection * connexion,int length);
void * parse_username_field(char * dgram, u_int8_t flag, int length ,connection * connexion);
int get_user_datas(connection *connexion,auth_field* packet_auth_field);
auth_field * parse_authentication_field(char * dgram,  u_int8_t flag ,int length);
int check_authentication(connection * connexion,auth_field * packet_auth_field );
int check_md5_sig(connection * connexion,md5_auth_field * authdatas );
void free_auth_field(auth_field * field);



/* garbage ;-) */
 void bail (const char *on_what);


/*
 * from userlogs.c
 */
 
int check_fill_user_counters(u_int16_t userid,long time,unsigned long packet_id,u_int32_t ip);
void print_users_list();
void log_new_user(int id,u_int32_t ip);
GModule * logs_module;
void log_user_packet (connection element,int state);
void real_log_user_packet (gpointer userdata, gpointer data);
int (*module_user_logs) (connection element, int state);
/*
 * External auth  stuff
 */

GModule * auth_module;
GPrivate* ldap_priv; /* private pointer to ldap connection */
GPrivate* dbm_priv; /* private pointer for dbm file access */
GPrivate* pgsql_priv; /* private pointer for pgsql database access */
GPrivate* mysql_priv; /* private pointer for mysql database access */
GSList * (*module_acl_check) (connection* element);
GSList * (*module_user_check) (u_int16_t userid,char *passwd);
GSList * (*module_user_check_v2) (connection * connexion,auth_field * packet_auth_field);
int init_ldap_system(void);
