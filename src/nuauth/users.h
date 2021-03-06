/*
 ** Copyright(C) 2005-2009 INL
 ** Written by Eric Leblond <regit@inl.fr>
 ** INL http://www.inl.fr/
 **
 ** $Id$
 **
 ** This program is free software; you can redistribute it and/or modify
 ** it under the terms of the GNU General Public License as published by
 ** the Free Software Foundation, version 3 of the License.
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

#ifndef USERS_H
#define USERS_H

#include <nussl.h>
#include "cache.h"

int init_user_cache();

void get_users_from_cache(connection_t * conn_elt);
gpointer user_duplicate_key(gpointer datas);

struct user_cached_datas {
	uint32_t uid;
	GSList *groups;
};

typedef enum {
	AUTH_TYPE_EXTERNAL, /*!< authentication SSL */
	AUTH_TYPE_INTERNAL, /*!< authentication SASL */
} auth_type_t;

typedef enum {
	PCKT_ERROR,
	PCKT_SPOOFED,
	PCKT_HELLO,
	PCKT_RECEIVE,
} pckt_treat_t;


/**
 * \brief Stores all information relative to a TLS user session.
 *
 * We don't want to have this information in all authentication packet.
 * Thus, once a user has managed to authenticate and has given
 * all the informations nuauth needs, we store it in this structure for
 * later use.
 *
 * When an authentication packet is received from the socket link to the user,
 * we add the informations contained in this strucuture to the just created
 * ::connection_t (see user_request()).
 *
 * An "user" is a person authenticated with a NuFW client.
 */
typedef struct {
	struct in6_addr addr;	/*!< \brief IPv6 address of the client */
	struct in6_addr server_addr;	/*!< \brief IPv6 address of the server */
	unsigned short sport;   /*!< \brief source port */
	/** \brief socket used by tls session.
	* It identify the client and it is used as the key
	*/
	int32_t socket;
	ev_io client_watcher;
	GAsyncQueue *workunits_queue;
	GMutex *rw_lock;
	/* tls should be removed by ssl */
	nussl_session *nussl;	/*!< \brief SSL session opened with tls_connect() */
	struct tls_user_context_t *srv_context;
	char *user_name;	/*!< \brief User name */
	uint32_t user_id;	/*!< \brief User identifier */
	GSList *groups;		/*!< \brief List of groups the user belongs to */
	gchar *sysname;		/*!< \brief OS system name (eg. "Linux") */
	gchar *release;		/*!< \brief OS release (eg. "2.6.12") */
	gchar *version;		/*!< \brief OS full version */
	gchar *client_name;	/*!< \brief Client full name */
	gchar *client_version;	/*!< \brief Client full version */
	uint32_t capa_flags;	/*!< \brief Handle client capabilities */
	time_t expire;		/*!< \brief Timeout of the session (-1 means unlimited) */
	int proto_version;	/*!< \brief Client protocol version */
	auth_type_t auth_type;
	int auth_quality;
	time_t connect_timestamp;
	time_t last_request;
	int rx_pckts;
	int error_pckts;
	int spoofed_pckts;
	int hello_pckts;
	gboolean activated;	/*!< \brief TRUE if user server listen for event for this session */
	gboolean pending_disconnect; /*!< \brief TRUE if session must be killed during loop reinjection */
} user_session_t;

char *capa_array[32];


nu_error_t increase_user_counter(user_session_t *usersession, pckt_treat_t ev);

#endif
