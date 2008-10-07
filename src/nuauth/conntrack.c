/*
 ** Copyright(C) 2005-2007 INL
 ** Written by Eric Leblond <regit@inl.fr>
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

#include "auth_srv.h"

/** \ingroup Nuauth
 *  \defgroup NuauthConntrack Fixed timeout connections handling
 *  @{
 */

/**
 * \file nuauth/conntrack.c
 * \brief Conntrack handling (used for fixed timeout)
 */

/** Send conntrack message to nufw server
 *
 * \param lconn Pointer to a ::limited_connection which contains informations about the connection to modify
 * \param msgtype Action to take against connection
 * \return a ::nu_error_t
 */

nu_error_t send_conntrack_message(struct limited_connection * lconn,
				  unsigned char msgtype)
{
	nufw_session_t *session = NULL;
	int ret = 0;

	debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_GW,
			  "going to send conntrack message");
	session = acquire_nufw_session_by_addr(&lconn->gwaddr);
	if (session) {
		switch (session->proto_version) {
			case PROTO_VERSION_NUFW_V22_2:
				{
					struct nuv4_conntrack_message_t
						message;
					/* send message */
					message.protocol_version =
						PROTO_VERSION_NUFW_V22_2;
					message.msg_type = msgtype;
					if (lconn->expire != -1) {
						message.timeout =
							htonl(lconn->expire -
									time(NULL));
					} else {
						debug_log_message(WARNING,
								DEBUG_AREA_PACKET,
								"not modifying fixed timeout");
						message.timeout = 0;
					}
					message.ip_protocol =
						lconn->tracking.protocol;

					message.ip_src.s6_addr32[0] =
						lconn->tracking.saddr.
						s6_addr32[0];
					message.ip_src.s6_addr32[1] =
						lconn->tracking.saddr.
						s6_addr32[1];
					message.ip_src.s6_addr32[2] =
						lconn->tracking.saddr.
						s6_addr32[2];
					message.ip_src.s6_addr32[3] =
						lconn->tracking.saddr.
						s6_addr32[3];

					message.ip_dst.s6_addr32[0] =
						lconn->tracking.daddr.
						s6_addr32[0];
					message.ip_dst.s6_addr32[1] =
						lconn->tracking.daddr.
						s6_addr32[1];
					message.ip_dst.s6_addr32[2] =
						lconn->tracking.daddr.
						s6_addr32[2];
					message.ip_dst.s6_addr32[3] =
						lconn->tracking.daddr.
						s6_addr32[3];
					message.msg_length = htons(sizeof(message));

					if ((message.ip_protocol ==
								IPPROTO_ICMP)
							|| (message.ip_protocol ==
								IPPROTO_ICMPV6)) {
						message.src_port =
							lconn->tracking.type;
						message.dest_port =
							lconn->tracking.code;
					} else {
						message.src_port =
							htons(lconn->tracking.
									source);
						message.dest_port =
							htons(lconn->tracking.
									dest);
					}
					ret = nufw_session_send(
							session,
							(char *) &message,
							sizeof
							(message));
					if (ret != NU_EXIT_OK) {
						declare_dead_nufw_session(session);
						return NU_EXIT_ERROR;
					} else {
						release_nufw_session(session);
					}
				}

				break;
			case PROTO_VERSION_NUFW_V20:
				{
					struct nuv3_conntrack_message_t
						message;
					/* send message */
					message.protocol_version =
						PROTO_VERSION_NUFW_V20;
					message.msg_type = msgtype;
					if (lconn->expire != -1) {
						message.timeout =
							htonl(lconn->expire -
									time(NULL));
					} else {
						debug_log_message(WARNING,
								DEBUG_AREA_PACKET,
								"not modifying fixed timeout");
						message.timeout = 0;
					}
					message.ipv4_protocol =
						lconn->tracking.protocol;
					message.ipv4_src =
						lconn->tracking.saddr.
						s6_addr[3];
					message.ipv4_dst =
						lconn->tracking.daddr.
						s6_addr[3];
					if (message.ipv4_protocol ==
							IPPROTO_ICMP) {
						message.src_port =
							lconn->tracking.type;
						message.dest_port =
							lconn->tracking.code;
					} else {
						message.src_port =
							htons(lconn->tracking.
									source);
						message.dest_port =
							htons(lconn->tracking.
									dest);
					}
					ret = nufw_session_send(
							session,
							(char *) &message,
							sizeof
							(message));
					if (ret != NU_EXIT_OK) {
						declare_dead_nufw_session(session);
						return NU_EXIT_ERROR;
					} else {
						release_nufw_session(session);
					}
				}

				break;
			default:
				log_message(WARNING, DEBUG_AREA_GW,
						"Invalid protocol %d",
						session->proto_version);
				release_nufw_session(session);
				return NU_EXIT_ERROR;
		}
	} else {
		log_message(WARNING, DEBUG_AREA_GW,
				"correct session not found among nufw servers");
		return NU_EXIT_ERROR;
	}
	return NU_EXIT_OK;
}

void send_destroy_message_and_free(gpointer user_data)
{
	struct limited_connection *data =
	    (struct limited_connection *) user_data;
	debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_PACKET | DEBUG_AREA_GW,
			  "connection will be destroyed");
	/* look for corresponding nufw tls session */
	send_conntrack_message(data, AUTH_CONN_DESTROY);
	/* free */
	g_free(data);
}

/**
 * get old entry
 */

static gboolean get_old_entry(gpointer key, gpointer value,
			      gpointer user_data)
{
	if (((struct limited_connection *) value)->expire <
	    GPOINTER_TO_INT(user_data)) {
		debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_PACKET | DEBUG_AREA_GW,
				  "found connection to be destroyed");
		return TRUE;
	} else {
		return FALSE;
	}
}

/**
 * search and destroy expired connections
 */

void destroy_expired_connection(GHashTable * lim_conn_list)
{
	g_hash_table_foreach_remove(lim_conn_list,
				    get_old_entry,
				    GUINT_TO_POINTER(time(NULL)));
}



/**
 *\brief Unique thread to be able to access to list of connections to expire.
 * Wait for messages
 */
void *limited_connection_handler(GMutex * mutex)
{
	GHashTable *lim_conn_list;
	struct internal_message *message = NULL;
	struct limited_connection *elt;
	GTimeVal tv;

	nuauthdatas->limited_connections_queue = g_async_queue_new();
	/* initialize packets list */
	lim_conn_list = g_hash_table_new_full((GHashFunc) hash_connection,
					      (GEqualFunc) tracking_equal,
					      NULL,
					      (GDestroyNotify)
					      send_destroy_message_and_free);
	g_async_queue_ref(nuauthdatas->limited_connections_queue);

	while (g_mutex_trylock(mutex)) {
		g_mutex_unlock(mutex);

		/* wait for message */
		g_get_current_time(&tv);
		g_time_val_add(&tv, POP_DELAY);
		message =
		    g_async_queue_timed_pop(nuauthdatas->
					    limited_connections_queue,
					    &tv);
		if (message == NULL)
			continue;

		switch (message->type) {
		case INSERT_MESSAGE:
			g_hash_table_insert(lim_conn_list,
					    &(((struct limited_connection
						*) message->datas)->
					      tracking), message->datas);
			break;

		case REFRESH_MESSAGE:
			destroy_expired_connection(lim_conn_list);
			break;

		case FREE_MESSAGE:
			elt =
			    (struct limited_connection *)
			    g_hash_table_lookup(lim_conn_list,
						message->datas);
			if (elt) {
				elt->expire = 0;
				g_hash_table_remove(lim_conn_list,
						    message->datas);
			}
#ifdef DEBUG_ENABLE
			else {
				log_message(VERBOSE_DEBUG, DEBUG_AREA_PACKET,
					    "connection not found can not be destroyed");
			}
#endif
			g_free(message->datas);
			break;

		case UPDATE_MESSAGE:
		/** here we get message from nufw kernel connection is ASSURED
                 * we have to limit it if needed and log the state change if needed */
			debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_GW,
					  "received update message for a conntrack entry");
			elt =
			    (struct limited_connection *)
			    g_hash_table_lookup(lim_conn_list,
						message->datas);
			if (elt == NULL) {
				debug_log_message(VERBOSE_DEBUG, DEBUG_AREA_GW | DEBUG_AREA_PACKET,
						  "Can't find conntrack entry to update");
			} else {
				if (nuauthconf->nufw_has_fixed_timeout) {
					send_conntrack_message(elt,
							       AUTH_CONN_UPDATE);
					/* this has to be removed from hash */
					g_hash_table_steal(lim_conn_list,
							   message->datas);
					g_free(elt);
				}
			}

			g_free(message->datas);
			break;

		default:
			g_free(message->datas);
			break;
		}
		g_free(message);
	}
	g_async_queue_unref(nuauthdatas->limited_connections_queue);
	g_hash_table_destroy(lim_conn_list);
	return NULL;
}

/** @} */
