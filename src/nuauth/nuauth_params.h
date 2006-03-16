/*
 ** Copyright(C) 2003-2005 Eric Leblond <regit@inl.fr>
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

#ifndef NUAUTH_PARAMS_H
#define NUAUTH_PARAMS_H

struct nuauth_params
{
    /* Sockets related */
    int authreq_port;
    int userpckt_port;

    /* global configuration variables */
    int packet_timeout;

    /** 
     * User session duration in second, default value: ::SESSION_DURATION
     */
    int session_duration;

    /* debug related */
    int debug_level;
    int debug_areas;

    /* logging related */
    int log_users;
    int log_users_sync;

    /* TODO switch this on a per-module basis */
    int log_users_strict;
    int log_users_without_realm;

    /* decision related */
    int prio_to_nok;

    /** policy on user connection, one of
     * - POLICY_MULTIPLE_LOGIN
     * - POLICY_ONE_LOGIN
     * - POLICY_PER_IP_ONE_LOGIN
     */
    int connect_policy;

    /* Use UTF-8 charset in exchanges */
    int uses_utf8;

    int push;
    int do_ip_authentication;
    int hello_authentication;
    struct in_addr* nufw_srv;
    struct in_addr* client_srv;
    
    /* cache setting */
    int datas_persistance;
    int acl_cache;   /* cache variables for acl cache */
    int user_cache;  /* cache variables for user cache */
    
    /* Multi user related variables */
    struct in_addr *authorized_servers;  /* authorized server list */
    char** multi_users_array;            /* multi users clients */
    struct in_addr * multi_servers_array;
    
    /* period definition */
    GHashTable* periods;

    /* performance tuning */
    int nbacl_check;
    int nbipauth_check;
    int nbuser_check;
    int nbloggers;
    int nb_session_loggers;
};

struct nuauth_thread_t
{
    GThread *thread;
    GMutex *mutex;
};    


#define POLICY_MULTIPLE_LOGIN 0
#define POLICY_ONE_LOGIN 1
#define POLICY_PER_IP_ONE_LOGIN 2

struct nuauth_datas
{
    /* main threads */
    struct nuauth_thread_t tls_auth_server;
    struct nuauth_thread_t tls_nufw_server;
    struct nuauth_thread_t tls_pusher;
    struct nuauth_thread_t search_and_fill_worker;
    struct nuauth_thread_t localid_auth_thread;
    struct nuauth_thread_t limited_connections_handler;

    /**
     * pool of thread which treat user packet.
     */
    GThreadPool* user_checkers;

    /**
     * pool of thread which treat nufw packet.
     */
    GThreadPool* acl_checkers;
    GThreadPool* user_loggers; 
    GThreadPool* user_session_loggers; 
    GThreadPool* decisions_workers;
    GThreadPool*  ip_authentication_workers;

    /* private datas */
    GPrivate *aclqueue;
    GPrivate *userqueue;

    GAsyncQueue* connections_queue;
    GAsyncQueue* limited_connections_queue;
    GAsyncQueue* tls_push_queue;
    GAsyncQueue* localid_auth_queue;

    /* cache related structure */
    struct cache_init_datas *user_cache;
    struct cache_init_datas *acl_cache;

    /* reload related */
    gint need_reload;
    gint locked_threads_number;
    GCond *reload_cond;
    GMutex *reload_cond_mutex;

    /** list of loaded modules */
    GSList *modules;

    /* signals */
    struct sigaction old_sigint_hdl;
    struct sigaction old_sigterm_hdl;
};

struct nuauth_params *nuauthconf;
struct nuauth_datas *nuauthdatas;
#endif
