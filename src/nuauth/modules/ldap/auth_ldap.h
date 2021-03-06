/*
** Copyright(C) 2003-2007 INL
**              Written by Eric Leblond <regit@inl.fr>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; version 3 of the License.
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

#include <sys/time.h>
#include <ldap.h>


GPrivate *ldap_priv;		/* private pointer to ldap connection */

#define LDAP_SERVER "127.0.0.1"
#define LDAP_SERVER_PORT 389
/*#define LDAP_USER NULL
#define LDAP_CRED NULL */
#define LDAP_USER "cn=admin,dc=nufw,dc=org"
#define LDAP_CRED "mypassword"
#define LDAP_REQUEST_TIMEOUT 10
#define LDAP_BASE "dc=nufw,dc=org"

/* Maximum size of a LDAP query */
#define LDAP_QUERY_SIZE 512

struct ldap_params {
	int ldap_request_timeout;
	char *binddn;
	char *bindpasswd;
	char *ldap_server;
	char *ldap_acls_base_dn;
	char *ldap_acls_timerange_base_dn;
	char *ldap_users_base_dn;
	int ldap_server_port;
	int ldap_filter_type;
	int ldap_use_ipv4_schema;
	GPrivate *ldap_priv;
};
