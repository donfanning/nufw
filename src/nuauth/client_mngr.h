/*
 ** Copyright(C) 2005 Eric Leblond <regit@inl.fr>
 **                  INL http://www.inl.fr/
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

#ifndef CLIENT_MNGR_H
#define CLIENT_MNGR_H

void init_client_struct();

void add_client(int socket, gpointer datas);

char delete_client_by_socket(int c);

inline user_session * get_client_datas_by_socket(int c);

inline GSList * get_client_sockets_by_ip(uint32_t ip);

void clean_session(user_session*);


struct msg_addr_set {
	uint32_t addr;
	struct nuv2_srv_message* msg;
	gboolean found;
};


char warn_clients(struct msg_addr_set * global_msg);

#endif

