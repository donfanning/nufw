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

#ifndef MODULES_H
#define MODULES_H

#define INIT_MODULE_FROM_CONF "init_module_from_conf"
#define MODULE_PARAMS_UNLOAD "module_params_unload"

typedef gboolean module_params_unload_t (gpointer params);

typedef struct module_T {
    gchar* name;
    gchar* module_name;
    GModule* module;
    gchar* configfile;
    gpointer func;
    gpointer params;
    module_params_unload_t* free_params;
} module_t;


int init_modules_system();
int load_modules();
void unload_modules();

int modules_user_check (const char *user, const char *pass,unsigned passlen,uint32_t *uid,GSList **groups);
GSList * modules_acl_check (connection_t* element);
/* ip auth */
gchar* modules_ip_auth(tracking_t *tracking);

int modules_user_logs (connection_t* element, tcp_state_t state);
int modules_user_session_logs(user_session* user, session_state_t state);

void modules_parse_periods(GHashTable* periods);

int modules_check_certificate (gnutls_session session, gnutls_x509_crt cert);

gchar* modules_certificate_to_uid (gnutls_session session, gnutls_x509_crt cert);

void block_on_conf_reload();
#endif
