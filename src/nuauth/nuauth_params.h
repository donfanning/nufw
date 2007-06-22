/*
 ** Copyright(C) 2003-2007 INL
 ** Written by Eric Leblond <regit@inl.fr>
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

#ifndef NUAUTH_PARAMS_H
#define NUAUTH_PARAMS_H

/** \addtogroup NuauthConf
 * @{
 */


/** Policy rule, see tls_sasl_connect_ok() */
typedef enum {
	PER_IP_TOO_MANY_LOGINS=1,
	PER_USER_TOO_MANY_LOGINS,
} policy_refused_reason_t;

/** \warn Deprecated. Do not use it !  */
typedef enum
{
	/** Allow multiple login per IP (accept any connection) (default rule) */
	POLICY_MULTIPLE_LOGIN=0,

	/** Allow an user can only be connected once (test based on username) */
	POLICY_ONE_LOGIN,

	/** Allow only an user session per IP (test based on IP) */
	POLICY_PER_IP_ONE_LOGIN
} policy_t;


struct nuauth_params {
	/* Sockets related */
	char *authreq_port;	/*<! Port used by nufw server to connect to nuauth */
	char *userpckt_port;	/*<! Port used by user to connect to nuauth */

	/* global configuration variables */
	int packet_timeout;	/*<! Timeout after which packet is cleaned by clean_connections_list() */

    /**
     * \brief User session duration in second
     *
     * Default value: ::SESSION_DURATION.
     *
     * A user
     * session is automatically disconnected after this duration.
     */
	int session_duration;

	/* debug related */
	int debug_level;
	int debug_areas;

	/* logging related */
	int log_users;
    /** \brief See log_user_packet() */
	int log_users_sync;

	/* \todo Switch this on a per-module basis */
	int log_users_strict;
	int log_users_without_realm;

    /** decision related, see take_decision()
     *
     * It is used to set acl policy:
     *  - if set to 1: if a DROP acl matches, the packet
     *  is dropped
     *  - if set to 0: if an ACCEPT acl matches, the packet
     *  is accepted
     */
	int prio_to_nok;

    /* Max number of client connections per user */
	int single_user_client_limit;
    /* Max number of client connections per IP */
	int single_ip_client_limit;

    /** When timeout is reached, use #DECISION_REJECT instead
     *  of #DECISION_DROP (if different than 0).
     *  Default value is 0. */
	int reject_after_timeout;

    /** \brief Uses REJECT instead of DROP if set.
     *
     * When an acl match but user is not in correct group, use #DECISION_REJECT instead
     *  of #DECISION_DROP (if different than 0).
     *  Default value is 0. */
	int reject_authenticated_drop;

    /** \brief Authentication use OLD compatble sasl method if set to 1 */
	int nuauth_uses_fake_sasl;

    /** \brief Use UTF-8 charset in exchanges if set to 1 */
	int uses_utf8;

    /** nuauth uses push mode if set to 1, else
     * the old poll mode is used */
	int push;
	int do_ip_authentication;	/*<! nuauth uses ip_authentication
					   fallback mode if set to 1 */
	int hello_authentication;
	char *nufw_srv;
	char *client_srv;
	int use_command_server;	/*<! Use command server? */

	/* cache setting */
	int datas_persistance;	/*<! time to keep data in cache before asking a refresh */
	int acl_cache;		/*<! nuauth uses acl cache  if set to 1 */
	int user_cache;		/*<! nuauth uses user cache if set to 1 */

	struct in6_addr *authorized_servers;	/*<! authorized nufw server list */

    /** Hash containing period definition */
	GHashTable *periods;

	/* option related of how nufw handle periods */
	gint nufw_has_conntrack;
	gint nufw_has_fixed_timeout;

	/* performance tuning */
	int nbacl_check;
	int nbipauth_check;
	int nbuser_check;
	int nbloggers;
	int nb_session_loggers;
};

struct nuauth_datas {
	/* main threads */
	struct nuauth_thread_t tls_auth_server;
	struct nuauth_thread_t tls_nufw_server;
	struct nuauth_thread_t tls_pusher;
	struct nuauth_thread_t search_and_fill_worker;
	struct nuauth_thread_t localid_auth_thread;
	struct nuauth_thread_t limited_connections_handler;
	struct nuauth_thread_t command_thread;
	struct nuauth_thread_t pre_client_thread;

    /**
     * pools of thread which treat user packet.
     */
	GThreadPool *user_checkers;
	GThreadPool *tls_sasl_worker;

    /**
     * pools of thread which treat nufw packet.
     */
	GThreadPool *acl_checkers;
	GThreadPool *user_loggers;
	GThreadPool *user_session_loggers;
	GThreadPool *decisions_workers;
	GThreadPool *ip_authentication_workers;

	/* private datas */
	GPrivate *aclqueue;
	GPrivate *userqueue;

	GAsyncQueue *connections_queue;
	GAsyncQueue *limited_connections_queue;
	GAsyncQueue *tls_push_queue;
	GAsyncQueue *localid_auth_queue;
	char *program_fullpath;

	/* cache related structure */
	cache_class_t *user_cache;
	cache_class_t *acl_cache;

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
	struct sigaction old_sighup_hdl;
};

struct nuauth_params *nuauthconf;
struct nuauth_datas *nuauthdatas;

/** @} */

#endif
