
/*
** Copyright(C) 2003 Eric Leblond <eric@regit.org>
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

void bail (const char *on_what){
  perror(on_what);
  exit(1);
}
/*
 * Args : a connection and a state
 */

inline char change_state(connection *elt, char state){
  static GStaticMutex state_mutex = G_STATIC_MUTEX_INIT;
  g_static_mutex_lock (&state_mutex);
  elt->state = state;
  g_static_mutex_unlock (&state_mutex);
  return  elt->state;
}

/* 
 *  get state with lock
 */

inline char get_state(connection *elt){
  static GStaticMutex state_mutex = G_STATIC_MUTEX_INIT;
  char state;
  g_static_mutex_lock (&state_mutex);
  state = elt->state;
  g_static_mutex_unlock (&state_mutex);
  return  state;
}


#if 0
guint
hash_connection(gconstpointer conn_p)
{
  connection * conn = (connection *)conn_p;
  return (jhash_3words((conn->tracking_hdrs).saddr,
	                     ((conn->tracking_hdrs).daddr ^ (conn->tracking_hdrs).protocol),
	                     ((conn->tracking_hdrs).dest | ((conn->tracking_hdrs).source << 16)),
	                     32));
}
#endif

gboolean compare_connection(gconstpointer tracking_hdrs1, gconstpointer tracking_hdrs2){
  /* compare IPheaders */
  if ( ( ((tracking *) tracking_hdrs1)->saddr ==
	 ((tracking *) tracking_hdrs2)->saddr ) &&
       ( ((tracking *) tracking_hdrs1)->daddr ==
	 ((tracking *) tracking_hdrs2)->daddr ) ){
 
    /* compare proto */
    if (((tracking *) tracking_hdrs1)->protocol ==
	((tracking *) tracking_hdrs2)->protocol) {
   
      /* compare proto headers */
      switch ( ((tracking *) tracking_hdrs1)->protocol) {
      case IPPROTO_TCP:
	if ( ( ((tracking *) tracking_hdrs1)->source ==
	       ((tracking *) tracking_hdrs2)->source )   &&
	     ( ((tracking *) tracking_hdrs1)->dest ==
	       ((tracking *) tracking_hdrs2)->dest ) ){
	
	  return TRUE;

	}
	break;
      case IPPROTO_UDP:
	if ( ( ((tracking *)tracking_hdrs1)->source ==
	       ((tracking *)tracking_hdrs2)->source )   &&
	     ( ((tracking *)tracking_hdrs1)->dest ==
	       ((tracking *)tracking_hdrs2)->dest ) ){
	  return TRUE;
	}
	break;
      case IPPROTO_ICMP:
	if ( ( ((tracking *)tracking_hdrs1)->type ==
	       ((tracking *)tracking_hdrs2)->type )   &&
	     ( ((tracking *)tracking_hdrs1)->code ==
	       ((tracking *)tracking_hdrs2)->code ) ){
	  return TRUE;
	}
      }
    }
  }
  return FALSE;
}

/* 
 * Try to insert a connection in Struct
 * Argument : a connection
 * Return : pointer to the connection list element
 */

connection * search_and_fill (connection * pckt) {
  connection * element = NULL;
  /* search pckt */
  g_static_mutex_lock (&insert_mutex);
  char has_changed_state=0;

  if (debug)
    printf("%d : starting search and fill\n",getpid());
  g_assert(pckt != NULL);
  element = (connection *) g_hash_table_lookup(conn_list,&(pckt->tracking_hdrs));
  if (element == NULL) {
    /* need to create Mutex */
    pckt->lock = g_mutex_new();
    g_assert(pckt->lock != NULL);	  
    if (debug)
      printf("%d : creating new element\n",getpid());
    g_hash_table_insert (conn_list,
			 &(pckt->tracking_hdrs),
			 pckt);
    /* as we append the new one is at the end */
     g_static_mutex_unlock (&insert_mutex);
    return pckt;
  } else {
    /* release global lock */
    g_static_mutex_unlock (&insert_mutex);
    /* and switch to element lock */
    LOCK_CONN(element);
    if (element->state == STATE_DONE) {
      if (debug)
	printf("%d : need only cleaning\n",getpid());
      conn_cl_delete(element);
      free_connection(pckt);
      return NULL ;
    }

    if (debug)
      printf("%d : filling connection\n",getpid());
    /* first check if we've only adding a packet_id */
    if ( ((connection *)element)->packet_id !=NULL && ( pckt->packet_id != NULL) ) {
      /* append id */
      if (debug)
	printf("%d : adding a packet_id to a connexion\n",getpid());
      ((connection *)element)->packet_id =
	g_slist_prepend(((connection *)element)->packet_id, GINT_TO_POINTER((pckt->packet_id)->data));
      UNLOCK_CONN(element);
      /* and return */
      return element;
    } else {
    	if ( (((connection *)element)->acl_groups == NULL)&& ( pckt->state == STATE_AUTHREQ  )) {
      if (debug)
	printf("%d : Fill acl datas\n",getpid());
      ((connection *)element)->acl_groups = pckt->acl_groups;
      ((connection *)element)->packet_id = pckt->packet_id;
      has_changed_state=1;
    } else { 
  	  if ( (((connection *)element)->user_groups == ALLGROUP) && (pckt->state == STATE_USERPCKT)) {
	    	  if (debug)
			printf("%d : Fill user datas\n",getpid());
    		  ((connection *)element)->user_groups = pckt->user_groups;
		  has_changed_state=1;
    	}
    	}
    }
    /* change STATE */
    if ( (((connection *)element)->state != STATE_DONE) && (has_changed_state == 1 )) {
      change_state(((connection *)element),((connection *)element)->state+pckt->state);
      if (debug)
	printf("%d : elt state : %d\n",getpid(),((connection *)element)->state);
    }
    UNLOCK_CONN(element);
    /* release memory used by pckt 
     * not using free_connection to do a complete free
     *because we have to keep the GSList
     */
    free(pckt);
    return element;
  }
  return NULL;
}

/*
 * print connection
 */
/* TODO : restore lock after DEBUG's done */
gint print_connection(gpointer data,gpointer userdata){
  struct in_addr src,dest;
  connection * conn=(connection *) data;
  //  LOCK_CONN(conn);
  src.s_addr = ntohl(conn->tracking_hdrs.saddr);
  dest.s_addr = ntohl(conn->tracking_hdrs.daddr);
  printf( "Connection :\n\tsrc=%s",
	  inet_ntoa(src));
  printf(" dst=%s proto=%u\n\t",
	 inet_ntoa(dest),
	 conn->tracking_hdrs.protocol);
  if (conn->tracking_hdrs.protocol == IPPROTO_TCP){
    printf("sport=%d dport=%d\n",
	   conn->tracking_hdrs.source,
	   conn->tracking_hdrs.dest);
  }
  //UNLOCK_CONN(conn);
  return 1;
}

gint free_struct(gpointer data,gpointer userdata){
  g_free((struct acl_group *)data);
  if (debug)
    printf("%d : free acl_groups at %p\n",getpid(),data);
  return 1;
}

/*
 * Send auth response to the gateway
 */

void send_auth_response(gpointer data, gpointer userdata){
  unsigned long  packet_id = GPOINTER_TO_UINT(data);
  uint8_t answer = GPOINTER_TO_UINT(userdata);
  uint8_t prio=1;
  uint8_t proto_version=PROTO_VERSION,answer_type=AUTH_ANSWER;
  char datas[512];
  char *pointer;
  int sck_auth_request;
  sck_auth_request = socket (PF_INET,SOCK_DGRAM,0);


  /* for each packet_id send a response */
  
  memset(datas,0,sizeof datas);
  memcpy(datas,&proto_version,sizeof proto_version);
  pointer=datas+sizeof proto_version;
  memcpy(pointer,&answer_type,sizeof answer_type);
  pointer+=sizeof answer_type;
  /* TODO id authsrv */
  pointer+=2;
  /* ANSWER and Prio */
  memcpy(pointer,&answer,sizeof answer);
  pointer+=sizeof answer;
  memcpy(pointer,&prio,sizeof prio);
  pointer+=sizeof prio;
  /* TODO id gw */
  pointer+=2;
  /* packet_id */
  memcpy(pointer,&(packet_id),sizeof(packet_id));
  pointer+=sizeof (packet_id);
  
  if (debug) {
    printf("%d : Sending auth answer %d for %lu on %d ... ",getpid(),answer,packet_id,sck_auth_request);
    fflush(stdout);
  }
  if (sendto(sck_auth_request,
	     datas,
	     pointer-datas,
	     MSG_DONTWAIT,
	     (struct sockaddr *)&adr_srv,
	     sizeof adr_srv) < 0) {
    g_warning("failure when sending auth response\n");
  }
  close(sck_auth_request);
  if (debug){
    printf("done\n");
  }
}



int free_connection(connection * conn){
  GSList *acllist;

  g_assert (conn != NULL );
  if (debug)
    if (conn->packet_id != NULL)
      printf("%d : freeing connection %p with %lu\n",getpid(),conn,(long unsigned int)GPOINTER_TO_UINT(conn->packet_id->data));
  acllist=conn->acl_groups;
  if ( (acllist  != DUMMYACLS) && (acllist  != NULL) ){
    g_slist_foreach(conn->acl_groups,(GFunc) free_struct,NULL);
    g_slist_free (acllist);
    if (debug)
      printf ("%d : acl_groups freed %p\n",getpid(),acllist);
  }
  if ( conn->user_groups != ALLGROUP )
    g_slist_free (conn->user_groups);
  if (conn->packet_id != NULL )
    g_slist_free (conn->packet_id);
  UNLOCK_CONN(conn);
  g_free(conn);
  return 1;
}

/* try to lock, if it fails this is because something is working on it so 
 * it should not be destroyed now
 */
int lock_and_free_connection (connection * conn) {
  GMutex * connlock=conn->lock;
  int freereturn=0;
  if ( g_mutex_trylock(connlock) ){
    freereturn = free_connection(conn);
    g_mutex_free(connlock);
    return freereturn;
  }
  return 0;
}

GSList * old_conn_list;

void  get_old_conn (gpointer key,
		    gpointer value,
		    gpointer user_data){
  long current_timestamp = GPOINTER_TO_INT(user_data);
  if ( current_timestamp - ((connection *)value)->timestamp > packet_timeout) {
    old_conn_list = (gpointer) g_slist_prepend( old_conn_list,key);
  }
}

int conn_cl_delete(gconstpointer conn) {
  g_assert (conn != NULL);
  g_hash_table_remove (conn_list,
				&(((connection *)conn)->tracking_hdrs));
  return 1;
}

int conn_key_delete(gconstpointer key) {
  connection* element = (connection*)g_hash_table_lookup ( conn_list,key);
  /* test for lock */
  if (element){
    if ( g_mutex_trylock(element->lock)) {
      g_hash_table_remove (conn_list,key);
      return 1;
    } else {
      if (debug)
	g_message("%d : element locked\n",getpid());
    }
  }
  return 0;
}


void clean_connections_list (){
  int conn_list_size=g_hash_table_size(conn_list);
  long current_timestamp=time(NULL);

  old_conn_list=NULL;
  //  static GStaticMutex insert_mutex = G_STATIC_MUTEX_INIT;
  g_static_mutex_lock (&insert_mutex);
  /* go through table and  stock keys associated to old packets */
  g_hash_table_foreach(conn_list,get_old_conn,GINT_TO_POINTER(current_timestamp));
  g_static_mutex_unlock (&insert_mutex);
  /* go through stocked keys to suppres old element */
  g_slist_foreach(old_conn_list,(GFunc)conn_key_delete,NULL);
  if (debug) {
    int conn_list_size_now=g_hash_table_size(conn_list);
    if (conn_list_size_now != conn_list_size)
      g_message("%d : %d connection(s) suppressed from list\n",getpid(),conn_list_size-conn_list_size_now);
  }
}

gint take_decision(connection * element) {
  GSList * parcours=NULL;
  int answer = NODECIDE;
  char test;
  GSList * user_group=element->user_groups;
  char init_state = get_state(element);
  
  if (debug)
    printf("%d : Try to take decision on %p\n",getpid(),element);
  /* first check if we have found acl */
  if ( element->acl_groups == NULL ){
    answer = NOK;
    if (debug){
      printf("%d : Did not find a ACL. Will drop Packet !\n",getpid());
    }
    if (element->packet_id != NULL )
      g_slist_foreach(element->packet_id,
		      (GFunc) send_auth_response,
		      GINT_TO_POINTER(answer)
		      );
   
    if (element->state == STATE_READY ){
      conn_cl_delete( element);
    } else {
      /* only change state */
      change_state(element,STATE_DONE);
      UNLOCK_CONN(element);
    }
  } else { 
    /* for each acl with NOK next OK  */
    for (test = OK; test != NOK  ; test = NOK   ) {
      for  ( parcours = element->acl_groups; 
	     ( parcours != NULL  && answer == NODECIDE ); 
	     parcours = g_slist_next(parcours) ) {
	/* for each user  group */
	for ( user_group = element->user_groups;
	      user_group != NULL && answer == NODECIDE;
	      user_group =  g_slist_next(user_group)) {
	  /* search user group in acl_groups */
	  if (g_slist_find(( (struct acl_group *)(parcours->data)   )->groups,(gconstpointer) user_group->data)) {
	    answer = test ;
	    break;
	  }
	}
      }
    }
  }

  /* send response if no one has changed state if packet's ready */
 
  if (element->state == init_state && (element->state == STATE_READY || answer == OK )) {
    if (debug)
      printf("%d : Proceed to decision %d for packet_id at %p\n",getpid(),answer,element->packet_id);
    g_slist_foreach(element->packet_id,
		    (GFunc) send_auth_response,
		    GINT_TO_POINTER(answer)
		    );
    /* delete element */
    if (element->state == STATE_READY ){
      conn_cl_delete(element);
    } else {
      /* only change state */
      change_state(element,STATE_DONE);
      UNLOCK_CONN(element);
    }
  } else {
    if (element->state == STATE_READY){
      send_auth_response(element->packet_id,NOK);
      conn_cl_delete(element);
    } else {
      if (debug)
	printf ("%d : Why did you bother me ?\n",getpid());
      UNLOCK_CONN(element);
    }
  }
  return 1;
}

