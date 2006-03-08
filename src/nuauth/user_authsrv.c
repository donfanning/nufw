/*
 * Copyright(C) 2004-2005 INL http://www.inl.fr/
 ** written by  Eric Leblond <regit@inl.fr>
 **             Vincent Deffontaines <vincent@inl.fr>
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

#include <auth_srv.h>
#include <crypt.h>
#include <sys/time.h>
#include <sasl/saslutil.h>

static GSList * userpckt_decode(struct tls_buffer_read * datas);

/**
 * Get user datas (containing datagram) and goes till inclusion
 * (or decision) on packet.
 *
 * \param userdata The datagram
 * \param data NULL (unused)
 */
void user_check_and_decide (gpointer userdata, gpointer data)
{
  GSList * conn_elts=NULL;
  GSList* conn_elt_l;
  connection_t* conn_elt;

  debug_log_message (VERBOSE_DEBUG, AREA_USER, "entering user_check\n");
  
  /* reload condition */
  block_on_conf_reload();
  conn_elts = userpckt_decode(userdata);

  if (conn_elts == NULL)
  {
      free_buffer_read(userdata);
      log_message (INFO, AREA_USER, "User packet decoding failed\n");
      return;
  }
  
  /* if OK search and fill */
  for (conn_elt_l=conn_elts; conn_elt_l!=NULL; conn_elt_l=conn_elt_l->next)
  {
      conn_elt = conn_elt_l->data;

      /* check source IP equality */
      if  (  ((struct tls_buffer_read *)userdata)->ipv4_addr 
              ==
              htonl(conn_elt->tracking.saddr) ){
#ifdef DEBUG_ENABLE
          if (DEBUG_OR_NOT(DEBUG_LEVEL_DEBUG,DEBUG_AREA_PACKET)){
              g_message("User : %s",conn_elt->username);
              print_connection(conn_elt,NULL);
          }
#endif
          if (conn_elt->packet_id){
              struct internal_message *message = g_new0(struct internal_message,1);
              message->type=INSERT_MESSAGE;
              message->datas=conn_elt;
              g_async_queue_push (nuauthdatas->localid_auth_queue,message);
          } else {
              g_async_queue_push (nuauthdatas->connections_queue,conn_elt);
          }
      } else {
          if (DEBUG_OR_NOT(DEBUG_LEVEL_INFO,DEBUG_AREA_USER)){
              struct in_addr badaddr;
              badaddr.s_addr=((struct tls_buffer_read *)userdata)->ipv4_addr;
              g_message("User %s on %s tried to authenticate packet from other ip\n",
                      conn_elt->username,
                      inet_ntoa(badaddr));
              print_connection(conn_elt,NULL);
          }
          /* free connection */
          free_connection(conn_elt);
      }
  }
  free_buffer_read(userdata);

  debug_log_message (VERBOSE_DEBUG, AREA_USER,
          "leaving user_check\n");
}

void user_process_field_hello(connection_t* connection, struct nuv2_authfield_hello* hellofield)
{
    g_message("got hello field");
    connection->packet_id=g_slist_prepend(NULL,GINT_TO_POINTER(hellofield->helloid));
}    

/**
 * \return If an error occurs returns -1, returns 1 otherwise
 */
int user_process_field_username(
        connection_t* connection, 
        uint8_t header_option,
        gboolean *multiclient_ok,
        struct nuv2_authfield_username *usernamefield)
{
    unsigned int len;
    gchar* dec_fieldname=NULL;
    unsigned int reallen=0;
    
    debug_log_message (VERBOSE_DEBUG, AREA_USER, "\tgot Username field");
    if (header_option != 0x1)
    {
        /* should not be here */
        log_message (INFO, AREA_USER,
            "not multiuser client but sent username field");
        return -1;
    }

    len=ntohs(usernamefield->length)-4;
    if (8*len > 2048)
    {
        /* it is reaaally long, we ignore packet (too lasy to kill client) */
        log_message (INFO, AREA_USER,
                "user packet announced a too long user name\n");
        return -1;
    }

    dec_fieldname =	g_new0(gchar,8*len);
    if (sasl_decode64((char*)usernamefield+4,len, dec_fieldname,8*len,&reallen) 
            ==
            SASL_BUFOVER) {
        dec_fieldname=g_try_realloc(dec_fieldname,reallen+1);
        if (dec_fieldname)
            sasl_decode64((char*)usernamefield+4,len, dec_fieldname,reallen,&reallen) ;
    } else {
        dec_fieldname=g_try_realloc(dec_fieldname,reallen+1);
    }
    dec_fieldname[reallen]=0;

    if (dec_fieldname != NULL)
    {
        connection->username= string_escape(dec_fieldname);
        if (connection->username == NULL)
            if (DEBUG_OR_NOT(DEBUG_LEVEL_WARNING,DEBUG_AREA_USER))
                g_warning("user packet received an invalid username\n");
    }else {
        g_free(dec_fieldname);
        if (DEBUG_OR_NOT(DEBUG_LEVEL_INFO,DEBUG_AREA_USER)){
            g_message("rejected packet, invalid username field");
        }
        return -1;
    }
    g_free(dec_fieldname);
    *multiclient_ok=TRUE;
    return 1;
}    

void user_process_field_ipv4(connection_t* connection, struct nuv2_authfield_ipv4 *ipfield)
{
    connection->tracking.saddr = ntohl(ipfield->src);
    connection->tracking.daddr = ntohl(ipfield->dst);
    connection->tracking.protocol = ipfield->proto;

    debug_log_message (VERBOSE_DEBUG, AREA_USER, "\tgot IPv4 field");
    switch (connection->tracking.protocol) 
    {
        case IPPROTO_TCP:
        case IPPROTO_UDP:
        default:
            connection->tracking.source=ntohs(ipfield->sport);
            connection->tracking.dest=ntohs(ipfield->dport);
            connection->tracking.type=0;
            connection->tracking.code=0;
            break;
            
        case IPPROTO_ICMP:
            connection->tracking.source=0;
            connection->tracking.dest=0;
            connection->tracking.type=ntohs(ipfield->sport);
            connection->tracking.code=ntohs(ipfield->dport);
            break;
    }
}    

int user_process_field_app(
        struct nuv2_authreq* authreq,
        connection_t* connection, 
        int field_buffer_len,
        struct nuv2_authfield_app *appfield)
{
    unsigned int reallen=0;
    gchar* dec_appname=NULL;
    unsigned int len=ntohs(appfield->length)-4;

    debug_log_message (VERBOSE_DEBUG, AREA_USER, "\tgot APP field");

    /* this has to be smaller than field size */
    if (field_buffer_len < ntohs(appfield->length))
    {
        if (DEBUG_OR_NOT(DEBUG_LEVEL_WARNING,DEBUG_AREA_USER))
            g_message("Improper application field length signaled in authreq header");
        return -1;
    }

    if (8*len > 2048){
        /* it is reaaally long, we ignore packet (too lasy to kill client) */
        log_message (INFO, AREA_USER,
            "user packet announced a too long app name\n");
        return -1;
    }
    dec_appname = g_new0(gchar,8*len);
    if (sasl_decode64((char*)appfield+4,len, dec_appname,8*len,&reallen) 
            ==
            SASL_BUFOVER) {
        dec_appname=g_try_realloc(dec_appname,reallen+1);
        if (dec_appname)
            sasl_decode64((char*)appfield+4,len, dec_appname,reallen,&reallen) ;
    } else {
        dec_appname=g_try_realloc(dec_appname,reallen+1);
    }
    dec_appname[reallen]=0;

    if (dec_appname != NULL)
    {
        connection->app_name= string_escape(dec_appname);
        if (connection->app_name == NULL)
            if (DEBUG_OR_NOT(DEBUG_LEVEL_WARNING,DEBUG_AREA_USER))
                g_warning("user packet received an invalid app name\n");
    }else{
        connection->app_name=NULL;
    }
    g_free(dec_appname);
    connection->app_md5=NULL;
    return 1;
}    


int user_process_field(
        struct nuv2_authreq* authreq, 
        uint8_t header_option,
        connection_t* connection, 
        gboolean *multiclient_ok,
        int auth_buffer_len,
        struct nuv2_authfield* field)
{
    /* check field length */
    field->length = ntohs(field->length);
    if (auth_buffer_len < field->length)
    {
        return -1;
    }

    switch (field->type) {
        case IPV4_FIELD:
            if (auth_buffer_len < sizeof(struct nuv2_authfield_ipv4)) {
                return -1;
            }
            user_process_field_ipv4(connection, (struct nuv2_authfield_ipv4 *)field);
            break;

        case APP_FIELD:
            if (auth_buffer_len < sizeof(struct nuv2_authfield_app)) {
                return -1;
            }
            if (user_process_field_app(authreq, connection, field->length, (struct nuv2_authfield_app *)field) < 0)
                return -1;
            break;

        case USERNAME_FIELD:
            if (auth_buffer_len < sizeof(struct nuv2_authfield_username)) {
                return -1;
            }
            if (user_process_field_username(connection, header_option, multiclient_ok, 
                        (struct nuv2_authfield_username *)field) < 0)
                return -1;
            break;

        case HELLO_FIELD:
            if (auth_buffer_len < sizeof(struct nuv2_authfield_hello)) {
                return -1;
            }
            user_process_field_hello(connection, (struct nuv2_authfield_hello *)field);
            break;

        default:
            log_message (INFO, AREA_USER,
                "unknown field type: %d", field->type);
            return -1;
    }
    return field->length;
}    

GSList* user_request(struct tls_buffer_read *datas)
{
    char * dgram = datas->buffer;
    struct nuv2_header* header = (struct nuv2_header *)dgram;
    GSList* conn_elts=NULL;
    connection_t* connection=NULL;
    char* start;
    gboolean multiclient_ok=FALSE;
    int buffer_len = datas->buffer_len;
    int auth_buffer_len;
    int field_length;
    struct nuv2_authreq* authreq;
    char *req_start;

    for (start = dgram + sizeof(struct nuv2_header), buffer_len -= sizeof(struct nuv2_header); 
         0 < buffer_len; 
         start += authreq->packet_length, buffer_len -= authreq->packet_length)
    {
        /* check buffer underflow */
        if (buffer_len < sizeof(struct nuv2_authreq))
        {
            free_connection_list(conn_elts);
            return NULL;
        }
        authreq=(struct nuv2_authreq* )start;

        authreq->packet_length=ntohs(authreq->packet_length);
        if (authreq->packet_length == 0
                || buffer_len < authreq->packet_length)
        {
            log_message (WARNING, AREA_USER,
                "Improper length signaled in authreq header: %d",
                authreq->packet_length);
            free_connection_list(conn_elts);
            return NULL;
        }

        connection = g_new0( connection_t,1);
        connection->acl_groups=NULL;
        connection->user_groups=NULL;
        connection->app_name=NULL;
        connection->app_md5=NULL;
        connection->username=NULL;
        connection->cacheduserdatas=NULL;
        connection->packet_id=NULL;
#ifdef PERF_DISPLAY_ENABLE
        gettimeofday(&(connection->arrival_time),NULL);
#endif
       
        /*** process all fields ***/
        debug_log_message (VERBOSE_DEBUG, AREA_USER, "Authreq start");
        req_start = start + sizeof(struct nuv2_authreq);
        auth_buffer_len = authreq->packet_length - sizeof(struct nuv2_authreq);
        for (; 
                0 < auth_buffer_len; 
                req_start += field_length, auth_buffer_len -= field_length)
        {
            struct nuv2_authfield* field = (struct nuv2_authfield* )req_start;

            /* check buffer underflow */
            if (auth_buffer_len < sizeof(struct nuv2_authfield))
            {
                free_connection_list(conn_elts);
                free_connection(connection);
                return NULL;
            }

            /* process field */
            field_length = user_process_field (authreq, header->option, 
                    connection, &multiclient_ok, auth_buffer_len, field);
            if (field_length < 0) {
                free_connection_list(conn_elts);
                free_connection(connection);
                return NULL;
            }
        }

        /* here all packet related information are filled-in */
        if (connection->username == NULL){	
            connection->username=g_strdup(datas->user_name);
        }
        connection->user_id=datas->user_id;
        connection->user_groups = g_slist_copy(datas->groups);
        connection->os_sysname=g_strdup(datas->os_sysname);
        connection->os_release=g_strdup(datas->os_release);
        connection->os_version=g_strdup(datas->os_version);
        if (connection->user_groups == NULL) {
            if ((header->option == 0x1) && multiclient_ok) {
                /* group is not fill in multi users mode
                 * need to be done now */
                log_message (INFO, AREA_USER, "Get users info");
                if ( nuauthconf->user_cache ){
                    get_users_from_cache(connection);
                } else {
                    if (user_check(
                                connection->username,
                                NULL,0,
                                &(connection->user_id),
                                &(connection->user_groups))!=SASL_OK)
                    {
                        log_message (INFO, AREA_PACKET, "User not found");
                    }
                }
            } else {
                log_message (INFO, AREA_USER, "User_check return is bad");
                free_connection_list(conn_elts);
                free_connection(connection);
                return NULL;
            }
        }
        
        connection->state=AUTH_STATE_USERPCKT;
        connection->acl_groups=NULL;            /* acl part is NULL */
        connection->timestamp=time(NULL);       /* first reset timestamp to now */
        conn_elts=g_slist_prepend(conn_elts,connection);

        debug_log_message (VERBOSE_DEBUG, AREA_USER, "Authreq end");
    }
    return conn_elts;
}    

/**
 * Decode user datagram packet and fill a connection with datas.
 */
static GSList * userpckt_decode(struct tls_buffer_read *datas)
{
    char * dgram = datas->buffer;
    struct nuv2_header* header=(struct nuv2_header*)dgram;

    /* check buffer underflow */
    if (datas->buffer_len < sizeof(struct nuv2_header))
    {
        return NULL;
    }

    /* check protocol version */
    if (header->proto != PROTO_VERSION)
    {
        log_message (INFO, AREA_USER,
            "unsupported protocol, got protocol %d (msg %d) with option %d (length %d)",
            header->proto, header->msg_type, header->option, header->length);
        return NULL;
    }

    header->length=ntohs(header->length);

    if(header->length > MAX_NUFW_PACKET_SIZE){
        log_message (WARNING, AREA_USER,
                "Improper length signaled in packet header");
        return NULL;
    }

    if (header->msg_type != USER_REQUEST)
    {
        log_message (INFO, AREA_USER, "unsupported message type");
        return NULL;
    }
    return user_request(datas);
}

