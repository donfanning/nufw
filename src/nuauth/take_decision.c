/*
 ** Copyright(C) 2006 INL
 **         written by Eric Leblond <regit@inl.fr>
 ** INL : http://www.inl.fr/
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

#include "auth_srv.h"
#include <netinet/ip.h>
#include <netinet/ip6.h>




/**
 * \ingroup NuauthCore
 *
 * @{
 */

static inline void update_connection_log_prefix(connection_t* element,const gchar* prefix)
{
  if (prefix) {
      g_free(element->log_prefix);
      element->log_prefix = g_strdup(prefix);
      debug_log_message(VERBOSE_DEBUG, AREA_MAIN,
                "Setting log prefix to %s", prefix);
  }
}

typedef enum {
    TEST_NODECIDE, /*<! Decision is not yet taken on packet */
    TEST_DECIDED /*<! Decision is taken on packet */
} test_t;

/**
 * \brief Take a decision of a connection authentification, and send it to NuFW.
 *
 * The process may be asynchronous (using decisions_workers,
 * member of ::nuauthdatas)
 *
 * It iters on each element of connection_t::acl_groups.
 * For each element, it test every groups to check
 * if the users belongs to one of them.
 * When a match is found, there is two cases:
 *  - if nuauth_params::prio_to_nok is set to one, we stop if the decision is to
 *  block the packet.
 *  - if nuauth_params::prio_to_nok is 0 then we continue till we fing a acl with
 *  ACCEPT decision.
 *
 * \param element A pointer to a ::connection_t
 * \param place Place where the connection is stored, see ::packet_place_t
 * \return Returns -1 if fails, 1 otherwise
 */
gint take_decision(connection_t *element, packet_place_t place)
{
    GSList * parcours = NULL;
    decision_t answer = DECISION_NODECIDE;
    test_t test;
    GSList * user_group = element->user_groups;
    time_t expire = -1; /* no expiration by default */

    debug_log_message (DEBUG, AREA_MAIN,
            "Trying to take decision on %p", element);

    /*even firster we check if we have an actual element*/
    if (element == NULL)
        return -1;

    /* first check if we have found acl */
    if ( element->acl_groups == NULL ){
        answer = DECISION_DROP;
    } else {
        decision_t start_test,stop_test;
        if (nuauthconf->prio_to_nok == 1){
            start_test = DECISION_ACCEPT;
            stop_test = DECISION_DROP;
        } else {
            start_test = DECISION_DROP;
            stop_test = DECISION_ACCEPT;
        }
        test = TEST_NODECIDE;
        for  ( parcours = element->acl_groups;
                ( parcours != NULL  && test == TEST_NODECIDE );
                parcours = g_slist_next(parcours) ) {
            /* for each user  group */
            if (parcours->data != NULL) {
                for ( user_group = element->user_groups;
                        user_group != NULL && test == TEST_NODECIDE;
                        user_group =  g_slist_next(user_group)) {
                    /* search user group in acl_groups */
                    g_assert(((struct acl_group *)(parcours->data))->groups);
                    if (g_slist_find(((struct acl_group *)(parcours->data))->groups,(gconstpointer)user_group->data)) {
                        /* find a group match, time to update decision */
                        answer = ((struct acl_group *)(parcours->data))->answer ;
                        if (nuauthconf->prio_to_nok == 1){
                            if ((answer == DECISION_DROP) || (answer == DECISION_REJECT)){
                                /* if prio is to not ok, then a DROP or REJECT is a final decision */
                                test = TEST_DECIDED;
                                update_connection_log_prefix(element,
                                        ((struct acl_group *)(parcours->data))->log_prefix
                                        );
                            } else {
                                /* we can have multiple accept, last one with a log prefix will be displayed */
                                update_connection_log_prefix(element,
                                        ((struct acl_group *)(parcours->data))->log_prefix
                                        );
                            }
                        } else {
                            if (answer == DECISION_ACCEPT){
                                test = TEST_DECIDED;
                                update_connection_log_prefix(element,
                                        ((struct acl_group *)(parcours->data))->log_prefix
                                        );
                            }
                        }
                        /* complete decision with check on period (This can change an ACCEPT answer) */
                        if (answer == DECISION_ACCEPT){
                            time_t periodend = -1;
                            /* compute end of period for this acl */
                            if (((struct acl_group *)(parcours->data))->period){
                                periodend = get_end_of_period_for_time_t(((struct acl_group *)(parcours->data))->period,time(NULL));
                                if (periodend == 0){
                                    /* this is not a correct time going to drop */
                                    answer = DECISION_NODECIDE;
                                    test = TEST_DECIDED;
                                    update_connection_log_prefix(element,
                                        ((struct acl_group *)(parcours->data))->log_prefix
                                        );
                                } else {
                                    debug_log_message(VERBOSE_DEBUG, AREA_MAIN,
                                            "end of period for %s in %ld", ((struct acl_group *)(parcours->data))->period,periodend);

                                }
                            }
                            if (
                                    (expire == -1) ||
                                    ((periodend != -1) && (expire !=-1) && (expire > periodend ))
                               ) {
                                debug_log_message(DEBUG, AREA_MAIN, " ... modifying expire");
                                expire = periodend;
                            }
                        }
                    } else {
                        if (answer == DECISION_NODECIDE) {
                            update_connection_log_prefix(element,
                                    ((struct acl_group *)(parcours->data))->log_prefix
                                    );
                        }
                    }
                } /* end of user group loop */
            } else {
                debug_log_message(DEBUG, AREA_MAIN, "Empty acl : bad things ...");
                answer = DECISION_DROP;
                test = TEST_DECIDED;
            }
        } /* end of acl groups loop */
    }

    /* answer is DECISION_NODECIDE if we did not found any matching group */
    if (answer == DECISION_NODECIDE){
	    if (nuauthconf->reject_authenticated_drop){
		    answer = DECISION_REJECT;
	    } else {
		    answer = DECISION_DROP;
	    }
    }
    if (expire == 0){
	    if (nuauthconf->reject_authenticated_drop){
		    answer = DECISION_REJECT;
	    } else {
		    answer = DECISION_DROP;
	    }
    }
    element->decision = answer;


    /* Call modules to do final tuning of packet (setting mark, expire modification ...) */
    modules_finalize_packet(element);

    if ((element->expire != -1) && (element->expire < expire)){
        debug_log_message(DEBUG, AREA_MAIN, " taken expire from element");
        expire = element->expire;
    }

    /* we must put element in expire list if needed before decision is taken */
    if (expire>0) {
        if (nuauthconf->nufw_has_conntrack){
            struct limited_connection* datas = g_new0(struct limited_connection,1);
            struct internal_message  *message = g_new0(struct internal_message,1);

            debug_log_message (VERBOSE_DEBUG, AREA_MAIN,
                    "Sending connection with fixed timeout to thread");
            memcpy(&(datas->tracking),&(element->tracking),sizeof(tracking_t));
            datas->expire = expire;
            datas->gwaddr = element->tls->peername;
            message->datas = datas;
            message->type = INSERT_MESSAGE;
            g_async_queue_push (nuauthdatas->limited_connections_queue, message);
        }
    }

    if (nuauthconf->log_users_sync) {
        /* copy current element */
        if (place == PACKET_IN_HASH){
            conn_cl_remove(element);
        }
        /* push element to decision workers */
        g_thread_pool_push (nuauthdatas->decisions_workers,
                element,
                NULL);
    } else {
        apply_decision(element);
        element->packet_id = NULL;
        if (place == PACKET_IN_HASH){
            conn_cl_delete(element);
        } else {
            free_connection(element);
        }

    }
    return 1;
}

/**
 * Log (using log_user_packet()) and send answer (using send_auth_response())
 * for a given connection.
 *
 * \param element A pointer to a ::connection_t
 * \return Returns 1
 */
gint apply_decision(connection_t *element)
{
    decision_t decision = element->decision;
#ifdef PERF_DISPLAY_ENABLE
    struct timeval leave_time,elapsed_time;
#endif

    if (element->state == AUTH_STATE_USERPCKT){
        debug_log_message( WARNING, AREA_MAIN,
                "BUG: Should not be in apply_decision for user only packet");
        return 1;
    }

    if (decision == DECISION_ACCEPT){
        log_user_packet(element,TCP_STATE_OPEN);
    } else {
        log_user_packet(element,TCP_STATE_DROP);
    }

    g_slist_foreach(element->packet_id,
            send_auth_response,
            element);
#ifdef PERF_DISPLAY_ENABLE
    gettimeofday(&leave_time,NULL);
    timeval_substract (&elapsed_time,&leave_time,&(element->arrival_time));
    log_message(INFO, AREA_MAIN,
            "Treatment time for conn : %ld.%03ld sec",
            elapsed_time.tv_sec,elapsed_time.tv_usec);
#endif

    /* free packet_id */
    if (element->packet_id != NULL ){
        g_slist_free (element->packet_id);
        element->packet_id = NULL;
    }
    return 1;
}

/**
 * This is a callback to apply a decision from the decision thread
 * pool (decisions_workers member of ::nuauthdatas).
 *
 * The queue is feeded by take_decision().
 *
 * \param userdata Pointer to a connection (of type ::connection_t)
 * \param data NULL pointer (unused)
 */
void decisions_queue_work (gpointer userdata, gpointer data)
{
    connection_t* element = (connection_t *)userdata;

    block_on_conf_reload();
    apply_decision(element);

    free_connection(element);
}

/**
 * Send authentification response (decision of type ::decision_t) to the NuFW.
 *
 * Use ::nuauth_decision_response_t structure to build the packet.
 *
 * \param packet_id_ptr NetFilter packet unique identifier (32 bits)
 * \param userdata Pointer to an answer of type ::auth_answer
 */
void send_auth_response(gpointer packet_id_ptr, gpointer userdata)
{
	connection_t * element = (connection_t *) userdata;
	uint32_t packet_id = GPOINTER_TO_UINT(packet_id_ptr);
	int payload_size = 0;
	int total_size = 0;
	char* buffer = NULL;

	switch(element->nufw_version){
		case PROTO_VERSION_V20:
			{
				nuv3_nuauth_decision_response_t* response = NULL;
				uint16_t mark16;
				/* check if user id fit in 16 bits */
				if (0xFFFF < element->mark) {
					log_message(WARNING, AREA_MAIN,
							"Mark don't fit in 16 bits, not to truncate the value.");
				}
				mark16 = (element->mark & 0xFFFF);
				if (element->decision == DECISION_REJECT){
					payload_size = IPHDR_REJECT_LENGTH + PAYLOAD_SAMPLE;
				}
				/* allocate */
				total_size = sizeof(nuv3_nuauth_decision_response_t)+payload_size;
				response = g_alloca(total_size);
				response->protocol_version = PROTO_VERSION_V20;
				response->msg_type = AUTH_ANSWER;
				response->mark = htons(mark16);
				response->decision = element->decision;
				response->priority = 1;
				response->padding = 0;
				response->packet_id = htonl(packet_id);
				response->payload_len = htons(payload_size);
				if (element->decision == DECISION_REJECT){
					char payload[IPHDR_REJECT_LENGTH + PAYLOAD_SAMPLE];
					struct iphdr *ip = (struct iphdr *)payload;

					/* create ip header */
					memset(payload, 0, IPHDR_REJECT_LENGTH );
					ip->version = AF_INET;
					ip->ihl = IPHDR_REJECT_LENGTH_BWORD;
					ip->tot_len = htons( IPHDR_REJECT_LENGTH + PAYLOAD_SAMPLE);
					ip->ttl = 64; /* write dummy ttl */
					ip->protocol = element->tracking.protocol;
					/* dummy convert to IPv4 as nufw on the other side does not support IPv6 at all */
					ip->saddr = htonl(element->tracking.saddr.s6_addr32[3]);
					ip->daddr = htonl(element->tracking.daddr.s6_addr32[3]);

					/* write transport layer */
					memcpy(payload+IPHDR_REJECT_LENGTH, element->tracking.payload, PAYLOAD_SAMPLE);

					/* write icmp reject packet */
					memcpy((char*)response+sizeof(nuv3_nuauth_decision_response_t), payload, payload_size);
				}

			buffer = (void*)response;
			}
			break;
	case PROTO_VERSION_V22:
	{
		nuv4_nuauth_decision_response_t* response = NULL;
		int use_icmp6;
		uint32_t mark = element->mark;

		use_icmp6 = (!is_ipv4(&element->tracking.saddr) || !is_ipv4(&element->tracking.daddr));

		if (element->decision == DECISION_REJECT){
			if (use_icmp6)
				payload_size = IP6HDR_REJECT_LENGTH + PAYLOAD6_SAMPLE;
			else
				payload_size = IPHDR_REJECT_LENGTH + PAYLOAD_SAMPLE;
		}
		/* allocate */
		total_size = sizeof(nuv4_nuauth_decision_response_t)+payload_size;
		response = g_alloca(total_size);
		response->protocol_version = PROTO_VERSION;
		response->msg_type = AUTH_ANSWER;
		response->tcmark = htonl(mark);
		response->decision = element->decision;
		response->priority = 1;
		response->padding = 0;
		response->packet_id = htonl(packet_id);
		response->payload_len = htons(payload_size);
		if (element->decision == DECISION_REJECT){
			if (use_icmp6) {
				char payload[IP6HDR_REJECT_LENGTH + PAYLOAD6_SAMPLE];
				struct ip6_hdr *ip = (struct ip6_hdr *)payload;

				/* create ip header */
				memset(payload, 0, IPHDR_REJECT_LENGTH );
				ip->ip6_flow = 0x60000000;
				ip->ip6_plen = htons(payload_size);
				ip->ip6_hops = 64; /* write dummy hop limit */
				ip->ip6_nxt = element->tracking.protocol;
				ip->ip6_src = element->tracking.saddr;
				ip->ip6_dst = element->tracking.daddr;

				/* write transport layer */
				memcpy(payload+IP6HDR_REJECT_LENGTH, element->tracking.payload, PAYLOAD6_SAMPLE);

				/* write icmp reject packet */
				memcpy((char*)response+sizeof(nuv4_nuauth_decision_response_t), payload, payload_size);
			} else {
				char payload[IPHDR_REJECT_LENGTH + PAYLOAD_SAMPLE];
				struct iphdr *ip = (struct iphdr *)payload;

				/* create ip header */
				memset(payload, 0, IPHDR_REJECT_LENGTH );
				ip->version = AF_INET;
				ip->ihl = IPHDR_REJECT_LENGTH_BWORD;
				ip->tot_len = htons( IPHDR_REJECT_LENGTH + PAYLOAD_SAMPLE);
				ip->ttl = 64; /* write dummy ttl */
				ip->protocol = element->tracking.protocol;
				ip->saddr = htonl(element->tracking.saddr.s6_addr32[3]);
				ip->daddr = htonl(element->tracking.daddr.s6_addr32[3]);

				/* write transport layer */
				memcpy(payload+IPHDR_REJECT_LENGTH, element->tracking.payload, PAYLOAD_SAMPLE);

				/* write icmp reject packet */
				memcpy((char*)response+sizeof(nuv4_nuauth_decision_response_t), payload, payload_size);
			}
		}

		buffer = (void*)response;
	}
	break;
    }

    debug_log_message (DEBUG, AREA_MAIN,
            "Sending auth answer %d for packet %u on TLS session %p",
            element->decision, packet_id, element->tls);
    if (element->tls->alive){
        g_mutex_lock(element->tls->tls_lock);
        gnutls_record_send(*(element->tls->tls), buffer, total_size);
        g_mutex_unlock(element->tls->tls_lock);
        (void)g_atomic_int_dec_and_test(&(element->tls->usage));
    } else {
        if (g_atomic_int_dec_and_test(&(element->tls->usage))){
            clean_nufw_session(element->tls);
        }
    }
}



/** @} */
