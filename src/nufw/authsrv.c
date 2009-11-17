/*
 ** Copyright (C) 2002-2009 INL
 ** Written by �ric Leblond <eric@regit.org>
 **            Vincent Deffontaines <vincent@gryzor.com>
 ** INL http://www.inl.fr/
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

#include "nufw.h"

#include <nubase.h>

/** \file nufw/authsrv.c
 *  \brief Process NuAuth packets
 *
 * authsrv() thread (created by auth_request_send()) wait for new NuAuth packets,
 * and then call auth_packet_to_decision() to process packet.
 */

/**
 * Process NuAuth message of type #AUTH_ANSWER
 */
int auth_process_answer(char *dgram, int dgram_size)
{
	nuv5_nuauth_decision_response_t *answer;
	uint32_t nfmark;
	int sandf;
	u_int32_t packet_id;
	int payload_len;

	/* check packet size */
	if (dgram_size < (int) sizeof(nuv4_nuauth_decision_response_t)) {
		return -1;
	}
	answer = (nuv5_nuauth_decision_response_t *) dgram;

	/* check payload length */
	payload_len = ntohs(answer->payload_len);
	if (dgram_size <
	    (int) (sizeof(nuv5_nuauth_decision_response_t) + payload_len)
	    || ((payload_len != 0) && (payload_len != (20 + 8))
		&& (payload_len != (40 + 8)) &&
		(dgram_size != (int) (sizeof(nuv5_nuauth_decision_response_t) + payload_len)))) {
		log_area_printf(DEBUG_AREA_GW, DEBUG_LEVEL_WARNING,
				"[!] Packet with improper size: payload of %d, received %d (vs %d)",
				payload_len,
				dgram_size,
				(int) (sizeof(nuv5_nuauth_decision_response_t) + payload_len));
		return -1;
	}

	/* get packet id and user id */
	packet_id = ntohl(answer->packet_id);

	/* search and destroy packet by packet_id */
	sandf = psearch_and_destroy(packet_id, &nfmark);
	if (!sandf) {
		log_area_printf(DEBUG_AREA_GW | DEBUG_AREA_GW,
				DEBUG_LEVEL_WARNING,
				"[!] Packet without a known ID: %u",
				packet_id);
		return -1;
	}

	switch (answer->decision) {
	case DECISION_ACCEPT:
		/* accept packet */
		debug_log_printf(DEBUG_AREA_PACKET,
				 DEBUG_LEVEL_VERBOSE_DEBUG,
				 "(*) Accepting packet with id=%u",
				 packet_id);
		if (nufw_set_mark) {
			debug_log_printf(DEBUG_AREA_PACKET,
					 DEBUG_LEVEL_VERBOSE_DEBUG,
					 "(*) Marking packet with %d",
					 ntohl(answer->tcmark));
#if HAVE_NFQ_MARK_EXPTIME
			if (ntohl(answer->expiration) != -1) {
				IPQ_SET_VWMARK_EXPTIME(packet_id, NF_ACCEPT,
					       answer->tcmark,
					       ntohl(answer->expiration));
			} else {
				IPQ_SET_VWMARK(packet_id, NF_ACCEPT,
					       answer->tcmark);
			}
#else
			IPQ_SET_VWMARK(packet_id, NF_ACCEPT,
				       answer->tcmark);
#endif
		} else {
			IPQ_SET_VERDICT(packet_id, NF_ACCEPT);
		}
		pckt_tx++;
		break;

	case DECISION_REJECT:
		/* Packet is rejected, ie. dropped and ICMP signalized */
		log_area_printf(DEBUG_AREA_PACKET, DEBUG_LEVEL_VERBOSE_DEBUG,
				"(*) Rejecting %" PRIu32, packet_id);
		IPQ_SET_VERDICT(packet_id, NF_DROP);
		if (send_icmp_unreach(dgram +
				  sizeof(nuv5_nuauth_decision_response_t),
				  payload_len) == -1) {
			log_area_printf(DEBUG_AREA_PACKET, DEBUG_LEVEL_WARNING,
					"(*) Could not sent ICMP reject for %" PRIu32, packet_id);
		}
		break;

	default:
		/* drop packet */
		debug_log_printf(DEBUG_AREA_PACKET,
				 DEBUG_LEVEL_VERBOSE_DEBUG,
				 "(*) Drop packet %u", packet_id);
		IPQ_SET_VERDICT(packet_id, NF_DROP);
	}
	return sizeof(nuv5_nuauth_decision_response_t) + payload_len;
}

/**
 * Process authentication server (NuAuth) packet answer. Different answers
 * can be:
 *   - Decision answer: packet accepted/rejected
 *   - Connection destroy: ask conntrack to destroy a connection
 *   - Connection update: ask conntrack to set connection timeout to given
 *     value
 *
 *  \return -1 in case of error
 */
static int auth_packet_to_decision(char *dgram, int dgram_size)
{
	if (dgram_size < 2) {
		debug_log_printf(DEBUG_AREA_GW, DEBUG_LEVEL_DEBUG,
				 "NuAuth sent too small message");
		return -1;
	}

	if (dgram[0] != PROTO_NUFW_VERSION) {
		debug_log_printf(DEBUG_AREA_GW, DEBUG_LEVEL_DEBUG,
				 "Wrong protocol version from authentication server answer.");
		return -1;
	}

	switch (dgram[1]) {
	case AUTH_ANSWER:
		return auth_process_answer(dgram, dgram_size);
	case AUTH_CONN_DESTROY:
		log_area_printf(DEBUG_AREA_MAIN | DEBUG_AREA_GW,
				DEBUG_LEVEL_WARNING,
				"Connection destroy message not supported");
		break;
	case AUTH_CONN_UPDATE:
		log_area_printf(DEBUG_AREA_MAIN | DEBUG_AREA_GW,
				DEBUG_LEVEL_WARNING,
				"Connection update message not supported");
		break;
	default:
		log_area_printf(DEBUG_AREA_GW, DEBUG_LEVEL_DEBUG,
				"NuAuth message type %d not for me",
				dgram[1]);
		break;
	}
	return -1;
}

/**
 * Thread waiting to authentication server (NuAuth) answer.
 * Call auth_packet_to_decision() on new packet.
 */
void *authsrv(void *data)
{
	int ret;
	int read_size;
	char cdgram[512];
	char *dgram = cdgram;

	log_area_printf(DEBUG_AREA_GW, DEBUG_LEVEL_VERBOSE_DEBUG,
			"[+] In auth server thread");

	/* memset(dgram, 0, sizeof dgram); */
	if (tls.session)
		ret = nussl_read(tls.session, dgram, sizeof cdgram);
	else
		ret = 0;
	if (ret == NUSSL_SOCK_TIMEOUT) {
		return NULL;
	}
	if (ret <= 0) {
		log_area_printf(DEBUG_AREA_GW, DEBUG_LEVEL_VERBOSE_DEBUG,
				"Error during nussl_read: %s", nussl_get_error(tls.session));
		close_tls_session();
		return NULL;
	} else {
		do {
			read_size = auth_packet_to_decision(dgram, ret);
			ret -= read_size;
			dgram = dgram + read_size;
		} while (ret > 0 && (read_size != -1));
	}

	dgram = cdgram;
	return NULL;
}
