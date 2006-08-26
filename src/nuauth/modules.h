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

/**
 * \ingroup NuauthModulesHandling
 * @{
 */

#define INIT_MODULE_FROM_CONF "init_module_from_conf"

typedef gboolean module_params_unload_t (gpointer params);

/**
 * Structure use to store a module instance
 */
typedef struct module_T {
    /**
     * Module name
     */
    gchar* name;

    /**
     * Module name
     */
    gchar* module_name;

    /**
     * glib module structure
     */ 
    GModule* module;

    /**
     * Filename of configuration file. If it's NULL,
     * you should use #DEFAULT_CONF_FILE.
     */
    gchar* configfile;

    /** 
     * Function used in the module:
     *   - user_check(): prototype is ::user_check_callback ;
     *   - acl_check(): prototype is ::acl_check_callback ;
     *   - define_periods(): prototype is ::define_period_callback ;
     *   - user_packet_logs(): prototype is ::user_logs_callback ;
     *   - user_session_logs(): prototype is ::user_session_logs_callback ;
     *   - ip_authentication(): prototype is ::ip_auth_callback ;
     *   - certificate_check(): prototype is ::certificate_check_callback ;
     *   - certificate_to_uid(): prototype is ::certificate_to_uid_callback.
     */
    gpointer func;

    /**
     * Structure where module store all its options
     */
    gpointer params;

    /**
     * Function used to unload module parameters
     */
    module_params_unload_t* free_params;
} module_t;


int init_modules_system();
int load_modules();
void unload_modules();

int modules_user_check (const char *user, const char *pass,unsigned passlen);
uint32_t modules_get_user_id (const char *user);
GSList* modules_get_user_groups (const char *user);

GSList * modules_acl_check (connection_t* element);
/* ip auth */
gchar* modules_ip_auth(tracking_t *tracking);

int modules_user_logs (void* element, tcp_state_t state);
int modules_user_session_logs(user_session_t* user, session_state_t state);

void modules_parse_periods(GHashTable* periods);

int modules_check_certificate (gnutls_session session, gnutls_x509_crt cert);

gchar* modules_certificate_to_uid (gnutls_session session, gnutls_x509_crt cert);

int modules_user_session_modify(user_session_t* c_session);

nu_error_t modules_finalise_packet(connection_t* connection);

gboolean nuauth_is_reloading();
void block_on_conf_reload();

typedef void (*cleanup_func_t) (void);
void cleanup_func_push(cleanup_func_t func);
void cleanup_func_remove(cleanup_func_t func);

/**
 * @}
 */

#endif
