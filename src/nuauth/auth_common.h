/*
 ** Copyright(C) 2005 Eric Leblond <regit@inl.fr>
 **                  INL http://www.inl.fr/
 **
 ** $Id$
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

#ifndef AUTH_COMMON_H
#define AUTH_COMMON_H

#define SHL32(x, n) (((n) < 32)?((x) << (n)):0)
#define SHR32(x, n) (((n) < 32)?((x) >> (n)):0)

typedef enum {
	PACKET_ALONE = 0,	/*!< The packet is not linked with the main hash ::conn_list */
	PACKET_IN_HASH		/*!< Packet is stored inside ::conn_list */
} packet_place_t;

void *search_and_fill(GMutex * mutex);

gboolean compare_connection(gconstpointer conn1, gconstpointer conn2);
int sck_auth_reply;
int conn_cl_remove(gconstpointer conn);
int conn_cl_delete(gconstpointer conn);
inline char get_state(connection_t * elt);
nu_error_t print_tracking_t(tracking_t *tracking);
gint print_connection(gpointer data, gpointer userdata);
void free_connection_list(GSList * list);
connection_t *duplicate_connection(connection_t * element);
void free_connection(connection_t * conn);
int lock_and_free_connection(connection_t * conn);
void clean_connections_list();
guint hash_connection(gconstpointer conn_p);

char *get_rid_of_domain(const char *user);
char *get_rid_of_prefix_domain(const char *user);

int is_ipv4(struct in6_addr *addr);

gboolean get_old_conn(gpointer key, gpointer value, gpointer user_data);


gboolean secure_snprintf(char *buffer, unsigned int buffer_size,
			 char *format, ...);


void free_buffer_read(struct tls_buffer_read *datas);

/*
 * Keep connection in a hash
 */

/** hash table containing the connections. */
GHashTable *conn_list;
/** global lock for the conn list. */
GStaticMutex insert_mutex;

#ifdef PERF_DISPLAY_ENABLE
int timeval_substract(struct timeval *result, struct timeval *x,
		      struct timeval *y);
#endif

nu_error_t check_protocol_version(int version);

int str_to_int(const char *text, int *value);
int str_to_uint32(const char *text, uint32_t *value);
int str_to_long(const char *text, long *value);
int str_to_ulong(const char *text, unsigned long *value);
char *int_to_str(int value);

void thread_pool_push(GThreadPool *pool, gpointer data, GError **error);

#endif
