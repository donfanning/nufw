/*
 ** Copyright(C) 2006, INL
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

#include <auth_srv.h>
#include <errno.h>

#include "pckt_authsrv_v3.h"

nu_error_t parse_dgram(connection_t * connection, unsigned char *dgram,
		       unsigned int dgram_size, connection_t ** conn,
		       nufw_message_t msg_type);

/**
 * Parse message content for message of type #AUTH_REQUEST or #AUTH_CONTROL
 * using structure ::nufw_to_nuauth_auth_message_t.
 *
 * \param dgram Pointer to packet datas
 * \param dgram_size Number of bytes in the packet
 * \param conn Pointer of pointer to the ::connection_t that we have to authenticate
 * \return A nu_error_t
 */
nu_error_t authpckt_new_connection_v3(unsigned char *dgram,
				      unsigned int dgram_size,
				      connection_t ** conn)
{
	nuv3_nufw_to_nuauth_auth_message_t *msg =
	    (nuv3_nufw_to_nuauth_auth_message_t *) dgram;
	connection_t *connection;
	nu_error_t ret;

	if (dgram_size < sizeof(nuv3_nufw_to_nuauth_auth_message_t)) {
		log_message(INFO, DEBUG_AREA_PACKET | DEBUG_AREA_GW,
			    "Undersized message from nufw server");
		return NU_EXIT_ERROR;
	}
	dgram += sizeof(nuv3_nufw_to_nuauth_auth_message_t);
	dgram_size -= sizeof(nuv3_nufw_to_nuauth_auth_message_t);

	/* allocate new connection */
	connection = g_new0(connection_t, 1);
	if (connection == NULL) {
		log_message(WARNING, DEBUG_AREA_PACKET,
			    "Can not allocate connection");
		return NU_EXIT_ERROR;
	}
#ifdef PERF_DISPLAY_ENABLE
	if (nuauthconf->debug_areas & DEBUG_AREA_PERF) {
		gettimeofday(&(connection->arrival_time), NULL);
	}
#endif
	connection->username = NULL;
	connection->acl_groups = NULL;
	connection->user_groups = NULL;
	connection->decision = DECISION_NODECIDE;
	connection->expire = -1;
	connection->flags = ACL_FLAGS_NONE;

	connection->packet_id =
	    g_slist_append(NULL, GUINT_TO_POINTER(ntohl(msg->packet_id)));
	debug_log_message(DEBUG, DEBUG_AREA_PACKET | DEBUG_AREA_GW,
			  "Auth pckt: Working on new connection (id=%u)",
			  (uint32_t) GPOINTER_TO_UINT(connection->
						      packet_id->data));

	/* timestamp */
	connection->timestamp = ntohl(msg->timestamp);
	if (connection->timestamp == 0)
		connection->timestamp = time(NULL);

	/* compat version: nufw is v2.0 */
	connection->nufw_version = PROTO_VERSION_NUFW_V20;

	ret =
	    parse_dgram(connection, dgram, dgram_size, conn,
			msg->msg_type);
	if (ret != NU_EXIT_CONTINUE) {
		return ret;
	}
#ifdef DEBUG_ENABLE
	if (DEBUG_OR_NOT(DEBUG_LEVEL_DEBUG, DEBUG_AREA_PACKET)) {
		print_connection(connection, "NuFW Packet");
	}
#endif
	*conn = connection;
	return NU_EXIT_OK;
}
