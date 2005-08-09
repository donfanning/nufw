/* $Id: packetsrv.c,v 1.8 2003/12/23 15:58:44 uid68721 Exp $ */

/*
 ** Copyright (C) 2002-2004 Eric Leblond <eric@regit.org>
 **		      Vincent Deffontaines <vincent@gryzor.com>
 **                    INL http://www.inl.fr/
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

#include "nufw.h"

/* 
 * return offset to next type of headers 
 */
int look_for_flags(char* dgram){
	struct iphdr * iphdrs = (struct iphdr *) dgram;
	/* check IP version */
	if (iphdrs->version == 4){
		if (iphdrs->protocol == IPPROTO_TCP){
			struct tcphdr * tcphdrs=(struct tcphdr *) (dgram+4*iphdrs->ihl);
			if (tcphdrs->fin || tcphdrs->ack || tcphdrs->rst ){
				return 1;
			}
		}
	}
	return 0;
}

#if USE_NFQUEUE
static int treat_packet(struct nfqnl_q_handle *qh, struct nfgenmsg *nfmsg,
	      struct nfattr *nfa[], void *data)
{
	packet_idl * current;
	if (nfa[NFQA_PACKET_HDR-1]) {
		struct nfqnl_msg_packet_hdr *ph = 
					NFA_DATA(nfa[NFQA_PACKET_HDR-1]);
		current->id= ntohl(ph->packet_id);
	}

	if (nfa[NFQA_PAYLOAD-1]) {
		printf("payload_len=%d ", NFA_PAYLOAD(nfa[NFQA_PAYLOAD-1]));
	}
	current=calloc(1,sizeof( packet_idl));
	if (current == NULL){
		if (DEBUG_OR_NOT(DEBUG_LEVEL_MESSAGE,DEBUG_AREA_MAIN)){
			if (log_engine == LOG_TO_SYSLOG) {
				syslog(SYSLOG_FACILITY(DEBUG_LEVEL_MESSAGE),"Can not allocate packet_id");
			} else {
				printf("[%i] Can not allocate packet_id\n",getpid());
			} 
		}
		return 0;
	}
#ifdef HAVE_LIBIPQ_MARK
	current->nfmark=msg_p->mark;
#endif
	current->timestamp=msg_p->timestamp_sec;
	/* lock packet list mutex */
	pthread_mutex_lock(&packets_list_mutex);
	/* Adding packet to list  */
	pcktid=padd(current);
	/* unlock datas */
	pthread_mutex_unlock(&packets_list_mutex);

	if (pcktid){
		/* send an auth request packet */
		if (! auth_request_send(AUTH_REQUEST,msg_p->packet_id,msg_p->payload,msg_p->data_len)){
			int sandf=0;
			/* we fail to send the packet so we free packet related to current */
			pthread_mutex_lock(&packets_list_mutex);
			/* search and destroy packet by packet_id */
			sandf = psearch_and_destroy (msg_p->packet_id,&msg_p->mark);
			pthread_mutex_unlock(&packets_list_mutex);

			if (!sandf){
				if (DEBUG_OR_NOT(DEBUG_LEVEL_WARNING,DEBUG_AREA_MAIN)){
					if (log_engine == LOG_TO_SYSLOG) {
						syslog(SYSLOG_FACILITY(DEBUG_LEVEL_WARNING),"Packet could not be removed : %lu",msg_p->packet_id);
					}else{
						printf("[%i] Packet could not be removed : %lu\n",getpid(),msg_p->packet_id);
					}
				}
			}
		}
	}
return 1;
}
#endif

void* packetsrv(){
	unsigned char buffer[BUFSIZ];
	int16_t size;
	uint32_t pcktid;
#if USE_NFQUEUE
	struct nfqnl_handle *h;
	struct nfnl_handle *nh;
	int fd;
	int rv;

	printf("opening library handle\n");
	h = nfqnl_open();
	if (!h) {
		fprintf(stderr, "error during nfqnl_open()\n");
		exit(1);
	}

	printf("unbinding existing nf_queue handler for AF_INET (if any)\n");
	if (nfqnl_unbind_pf(h, AF_INET) < 0) {
		fprintf(stderr, "error during nfqnl_unbind_pf()\n");
		exit(1);
	}

	printf("binding nfnetlink_queue as nf_queue handler for AF_INET\n");
	if (nfqnl_bind_pf(h, AF_INET) < 0) {
		fprintf(stderr, "error during nfqnl_bind_pf()\n");
		exit(1);
	}

	printf("binding this socket to queue '0'\n");
	hndl = nfqnl_create_queue(h,  0, &treat_packet, NULL);
	if (!hndl) {
		fprintf(stderr, "error during nfqnl_create_queue()\n");
		exit(1);
	}

	printf("setting copy_packet mode\n");
	if (nfqnl_set_mode(hndl, NFQNL_COPY_PACKET, 0xffff) < 0) {
		fprintf(stderr, "can't set packet_copy mode\n");
		exit(1);
	}

	nh = nfqnl_nfnlh(h);
	fd = nfnl_fd(nh);
#else
	ipq_packet_msg_t *msg_p = NULL ;
	packet_idl * current;
    /* init netlink connection */
    hndl = ipq_create_handle(0,PF_INET);
    if (hndl)
        ipq_set_mode(hndl, IPQ_COPY_PACKET,BUFSIZ);  
    else {
        if (DEBUG_OR_NOT(DEBUG_LEVEL_CRITICAL,DEBUG_AREA_MAIN)){
            if (log_engine == LOG_TO_SYSLOG){
                syslog(SYSLOG_FACILITY(DEBUG_LEVEL_CRITICAL),"Could not create ipq handle");
            }else{
                printf("[%d] Could not create ipq handle\n",getpid());
            }
        }
    }
#endif
	for (;;){
#if USE_NFQUEUE
		if (rv = recv(fd, buffer, sizeof(buffer), 0)) && rv >= 0) {
			nfqnl_handle_packet(h, buffer, rv);
		} else 
			break;
#else
		size = ipq_read(hndl,buffer,sizeof(buffer),0);
		if (size != -1){
			if (size < BUFSIZ ){
				if (ipq_message_type(buffer) == NLMSG_ERROR ){
					if (DEBUG_OR_NOT(DEBUG_LEVEL_MESSAGE,DEBUG_AREA_MAIN)){
						if (log_engine == LOG_TO_SYSLOG) {
							syslog(SYSLOG_FACILITY(DEBUG_LEVEL_MESSAGE),"Got error message from libipq : %d",ipq_get_msgerr(buffer));
						}else {
							printf("[%i] Got error message from libipq : %d\n",getpid(),ipq_get_msgerr(buffer));
						}
					}
				} else {
					if ( ipq_message_type (buffer) == IPQM_PACKET ) {
						pckt_rx++ ;
						/* printf("Working on IP packet\n"); */
						msg_p = ipq_get_packet(buffer);
						/* need to parse to see if it's an end connection packet */
						if (look_for_flags(msg_p->payload)){
							auth_request_send(AUTH_CONTROL,msg_p->packet_id,msg_p->payload,msg_p->data_len);
							IPQ_SET_VERDICT( msg_p->packet_id,NF_ACCEPT);
						} else {
							current=calloc(1,sizeof( packet_idl));
							if (current == NULL){
								if (DEBUG_OR_NOT(DEBUG_LEVEL_MESSAGE,DEBUG_AREA_MAIN)){
									if (log_engine == LOG_TO_SYSLOG) {
										syslog(SYSLOG_FACILITY(DEBUG_LEVEL_MESSAGE),"Can not allocate packet_id");
									} else {
										printf("[%i] Can not allocate packet_id\n",getpid());
									} 
								}
								return 0;
							}
							current->id=msg_p->packet_id;
#ifdef HAVE_LIBIPQ_MARK
							current->nfmark=msg_p->mark;
#endif
							current->timestamp=msg_p->timestamp_sec;
							/* lock packet list mutex */
							pthread_mutex_lock(&packets_list_mutex);
							/* Adding packet to list  */
							pcktid=padd(current);
							/* unlock datas */
							pthread_mutex_unlock(&packets_list_mutex);

							if (pcktid){
								/* send an auth request packet */
								if (! auth_request_send(AUTH_REQUEST,msg_p->packet_id,msg_p->payload,msg_p->data_len)){
									int sandf=0;
									/* we fail to send the packet so we free packet related to current */
									pthread_mutex_lock(&packets_list_mutex);
									/* search and destroy packet by packet_id */
									sandf = psearch_and_destroy (msg_p->packet_id,&msg_p->mark);
									pthread_mutex_unlock(&packets_list_mutex);

									if (!sandf){
										if (DEBUG_OR_NOT(DEBUG_LEVEL_WARNING,DEBUG_AREA_MAIN)){
											if (log_engine == LOG_TO_SYSLOG) {
												syslog(SYSLOG_FACILITY(DEBUG_LEVEL_WARNING),"Packet could not be removed : %lu",msg_p->packet_id);
											}else{
												printf("[%i] Packet could not be removed : %lu\n",getpid(),msg_p->packet_id);
											}
										}
									}
								}
							}
						}
					} else {
						if (DEBUG_OR_NOT(DEBUG_LEVEL_DEBUG,DEBUG_AREA_MAIN)){
							if (log_engine == LOG_TO_SYSLOG) {
								syslog(SYSLOG_FACILITY(DEBUG_LEVEL_DEBUG),"Dropping non-IP packet");
							}else {
								printf ("[%i] Dropping non-IP packet\n",getpid());
							}
						}
						IPQ_SET_VERDICT(msg_p->packet_id, NF_DROP);
					}
				}
			}
		} else {
			if (DEBUG_OR_NOT(DEBUG_LEVEL_DEBUG,DEBUG_AREA_MAIN)){
				if (log_engine == LOG_TO_SYSLOG) {
					syslog(SYSLOG_FACILITY(DEBUG_LEVEL_DEBUG),"BUFSIZ too small, (size == %d)",size);
				}else {
					printf("[%i] BUFSIZ too small, (size == %d)\n",getpid(),size);
				}
			}
		}
#endif
	}
#if USE_NFQUEUE

#else
	ipq_destroy_handle( hndl );  
#endif
}   

int auth_request_send(uint8_t type,unsigned long packet_id,char* payload,int data_len){
	char datas[512];
	char *pointer;
	int auth_len,total_data_len=512;
	uint8_t version=PROTO_VERSION;
	uint16_t dataslen=data_len+12;
	long timestamp;

	timestamp = time(NULL);

#ifdef WORDS_BIGENDIAN
	packet_id=swap32(packet_id);
	dataslen=swap16(dataslen);
	timestamp=swap32(timestamp);
#endif

	if ( ((struct iphdr *)payload)->version == 4) {
		memset(datas,0,sizeof datas);
		memcpy(datas,&version,sizeof version);
		pointer=datas+sizeof version;
		memcpy(pointer,&type,sizeof type);
		pointer+=sizeof type;
		memcpy(pointer,&dataslen,sizeof dataslen);
		pointer+=sizeof dataslen;
		memcpy(pointer,&packet_id,sizeof packet_id);
		pointer+=sizeof packet_id;
		memcpy(pointer,&timestamp,sizeof timestamp);
		pointer+=sizeof timestamp;
		auth_len=pointer-datas;

		/* memcpy header to datas + offset */
		if (data_len<512-auth_len) {
			memcpy(pointer,payload,data_len);
			total_data_len=data_len+auth_len;
		} else {
#ifdef DEBUG_ENABLE
			if (DEBUG_OR_NOT(DEBUG_LEVEL_DEBUG,DEBUG_AREA_MAIN)){
				if (log_engine == LOG_TO_SYSLOG) {
					syslog(SYSLOG_FACILITY(DEBUG_LEVEL_DEBUG),"Very long packet : truncating !");
				}else {
					printf("[%i] Very long packet : truncating !\n",getpid());
				}
			}
#endif
			memcpy(pointer,payload,511-auth_len);
		}

	} else {
#ifdef DEBUG_ENABLE
		if (DEBUG_OR_NOT(DEBUG_LEVEL_DEBUG,DEBUG_AREA_MAIN)){
			if (log_engine == LOG_TO_SYSLOG) {
				syslog(SYSLOG_FACILITY(DEBUG_LEVEL_WARNING),"Dropping non-IP packet");
			}else {
				printf ("[%i] Dropping non-IP packet\n",getpid());
			}
		}
#endif
		return 0;
	}


#ifdef DEBUG_ENABLE
	if (DEBUG_OR_NOT(DEBUG_LEVEL_INFO,DEBUG_AREA_MAIN)){
		if (log_engine == LOG_TO_SYSLOG) {
#ifdef WORDS_BIGENDIAN
			syslog(SYSLOG_FACILITY(DEBUG_LEVEL_DEBUG),"Sending request for %lu",swap32(packet_id));
#else
			syslog(SYSLOG_FACILITY(DEBUG_LEVEL_DEBUG),"Sending request for %lu",packet_id);
#endif
		}else {
#ifdef WORDS_BIGENDIAN
			printf("[%i] Sending request for %lu\n",getpid(),swap32(packet_id));
#else
			printf("[%i] Sending request for %lu\n",getpid(),packet_id);
#endif
		}
	}
#endif
	if (nufw_use_tls){
		/* negotiate TLS connection if needed */
		if (!tls.session){
			if (DEBUG_OR_NOT(DEBUG_LEVEL_INFO,DEBUG_AREA_MAIN)){
				if (log_engine == LOG_TO_SYSLOG) {
					syslog(SYSLOG_FACILITY(DEBUG_LEVEL_DEBUG),"Not connected, trying TLS connection");
				}else {
					printf("[%i] Not connected, trying TLS connection\n",getpid());
				}
			}
			tls.session = tls_connect();

			if (tls.session){
				if (DEBUG_OR_NOT(DEBUG_LEVEL_WARNING,DEBUG_AREA_MAIN)){
					if (log_engine == LOG_TO_SYSLOG) {
						syslog(SYSLOG_FACILITY(DEBUG_LEVEL_DEBUG),"Connection to nuauth restored");
					}else {
						printf("[%i] Connection to nuauth restored\n",getpid());
					}
				}
				tls.active=1;
				pthread_cond_signal(session_active_cond);
			}
		}
		if (tls.session){
			/* send packet */
			if (!gnutls_record_send(*(tls.session),datas,total_data_len)){
				int socket_tls;
				if (DEBUG_OR_NOT(DEBUG_LEVEL_WARNING,DEBUG_AREA_MAIN)){
					if (log_engine == LOG_TO_SYSLOG) {
						syslog(SYSLOG_FACILITY(DEBUG_LEVEL_CRITICAL),"tls send failure when sending request");
					}else {
						printf ("[%i] tls send failure when sending request\n",getpid());
					}
				}
				pthread_mutex_lock(session_active_mutex);
				if (tls.active){
					tls.active=0;
					//	gnutls_bye(*tls.session,GNUTLS_SHUT_WR);
					socket_tls=(int)gnutls_transport_get_ptr(*tls.session);
					shutdown(socket_tls,SHUT_RDWR);
				}
				pthread_cond_wait(session_destroyed_cond,session_active_mutex);
				return 0;
			} else {
				tls.active=1;
			}
		} 
	} else {
		if (sendto(sck_auth_request,
					datas,
					total_data_len,
					0,
					(struct sockaddr *)&adr_srv,
					sizeof adr_srv) < 0)

			if (DEBUG_OR_NOT(DEBUG_LEVEL_WARNING,DEBUG_AREA_MAIN)){
				if (log_engine == LOG_TO_SYSLOG) {
					syslog(SYSLOG_FACILITY(DEBUG_LEVEL_CRITICAL),"sendto() failure when sending request");
				}else {
					printf ("[%i] sendto() failure when sending request\n",getpid());
				}
			}
		return 0;
	}
	return 1;
}
