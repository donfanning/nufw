#!/usr/bin/python
from unittest import TestCase, main
from common import connectClient, startNufw, retry
from logging import info
from time import time, mktime
from inl_tests.iptables import Iptables
from socket import ntohl
from filter import testAllowPort, testDisallowPort, VALID_PORT, INVALID_PORT
from datetime import datetime
from IPy import IP
import platform
from os.path import basename, realpath
from sys import argv, executable
from nuauth import Nuauth
from nuauth_conf import NuauthConf
from plaintext import USERDB, PlaintextAcl
from config import config as test_config

def datetime2unix(timestamp):
    tm = timestamp.timetuple()
    return int(mktime(tm))

POSTGRESQL = False

config = NuauthConf()
if POSTGRESQL:
    import pgdb
    DB_PACKET_TABLE = config["pgsql_table_name"]
    DB_USER_TABLE = config["pgsql_users_table_name"]
    DB_SERVER = config["pgsql_server_addr"]
    DB_USER = config["pgsql_user"]
    DB_PASSWORD = config["pgsql_passwd"]
    DB_DBNAME = config["pgsql_db_name"]
    QUERY_TIMEOUT = test_config.getfloat('test_pgsql', 'query_timeout')
else:
    import MySQLdb
    DB_PACKET_TABLE = config["mysql_table_name"]
    DB_USER_TABLE = config["mysql_users_table_name"]
    DB_SERVER = config["mysql_server_addr"]
    DB_USER = config["mysql_user"]
    DB_PASSWORD = config["mysql_passwd"]
    DB_DBNAME = config["mysql_db_name"]
    QUERY_TIMEOUT = test_config.getfloat('test_mysql', 'query_timeout')

OS_SYSNAME = platform.system()    # 'Linux'
OS_RELEASE = platform.release()   # '2.6.19.2-haypo'
OS_VERSION = platform.version()   # '#2 Mon Feb 5 10:55:30 CET 2007'
CLIENT_OS = "-".join( (OS_SYSNAME, OS_VERSION, OS_RELEASE) )
CLIENT_APP = realpath(executable)
LOG_PREFIX = "42:ETH0-IF"
OOB_PREFIX = "%s ACCEPT" % LOG_PREFIX

def datetime_now(delta=0):
    # Use datetime.fromtimestamp() with int(time()) to have microsecond=0
    return datetime.fromtimestamp(int(time()+delta))
def datetime_before():
    return datetime_now(-1.1)
def datetime_after():
    return datetime_now(1.1)

def formatTimestamp(ts):
    if POSTGRESQL:
        return "ABSTIME(%s)" % ts
    else:
        return "FROM_UNIXTIME(%s)" % ts

class MysqlLog(TestCase):
    def setUp(self):
        startNufw(["-s"])
        config = NuauthConf()
        config["nuauth_log_users"] = '9'
        config["mysql_prefix_version"] = '1'
        if POSTGRESQL:
            config.need_restart = True
            self.conn = pgdb.connect(
                host=DB_SERVER,
                user=DB_USER,
                password=DB_PASSWORD,
                database=DB_DBNAME)
            config["nuauth_user_logs_module"] = '"pgsql"'
            config["nuauth_user_session_logs_module"] = '"pgsql"'
        else:
            self.conn = MySQLdb.Connect(
                host=DB_SERVER,
                user=DB_USER,
                passwd=DB_PASSWORD,
                db=DB_DBNAME)
            config["nuauth_user_logs_module"] = '"mysql"'
            config["nuauth_user_session_logs_module"] = '"mysql"'
        self.users = USERDB
        self.user = self.users[0]
        self.acls = PlaintextAcl()
        self.acls.addAcl("web", VALID_PORT, self.user.gid, log_prefix=LOG_PREFIX)
        self.users.install(config)
        self.acls.install(config)
        self.nuauth = Nuauth(config)
        self.start_time = int(time()-1.1)

    def query(self, sql):
        if POSTGRESQL:
            prefix = "PostgreSQL"
        else:
            prefix = "MySQL"
        info("%s query: %s" % (prefix, sql))
        cursor = self.conn.cursor()
        cursor.execute(sql)
        info("%s result: %s rows" % (prefix, cursor.rowcount))
        return cursor

    def fetchone(self, cursor):
        row = cursor.fetchone()
        if POSTGRESQL:
            info("PostgreSQL fetchone(): %s" % repr(row))
        else:
            info("MySQL fetchone(): %s" % repr(row))
        return row

    def tearDown(self):
        # Stop nuauth
        self.nuauth.stop()
        self.conn.close()
        self.users.desinstall()
        self.acls.desinstall()

    def _login(self, sql):
        # Client login
        client = self.user.createClientWithCerts()
        self.assert_(connectClient(client))

        # Check number of rows
        for when in retry(timeout=QUERY_TIMEOUT):
            cursor = self.query(sql)
            for line in self.nuauth.readlines():
                pass
            if cursor.rowcount:
                break
        self.assertEqual(cursor.rowcount, 1)

        # Read row columns
        (ip_saddr, user_id, username, os_sysname,
            os_release, os_version, end_time) = self.fetchone(cursor)
        if not POSTGRESQL:
            ip_saddr = ntohl(ip_saddr) & 0xFFFFFFFF

        # Check values
        self.assertEqual(IP(ip_saddr), client.ip)
        self.assertEqual(user_id, self.user.uid)
        self.assertEqual(username, client.username)
        self.assertEqual(os_sysname, OS_SYSNAME)
        self.assertEqual(os_release, OS_RELEASE)
        self.assertEqual(os_version, OS_VERSION)
        return client

    def _logout(self, sql, client):
        # Client logout
        # Use datetime.fromtimestamp() with int(time()) to have microsecond=0
        logout_before = datetime_before()
        client.stop()

        for when in retry(timeout=QUERY_TIMEOUT):
            # Get last MySQL row
            cursor = self.query(sql)

            # Check number of rows
            if not cursor.rowcount:
                continue
            self.assertEqual(cursor.rowcount, 1)

            # Read row columns
            (ip_saddr, user_id, username, os_sysname,
                os_release, os_version, end_time) = self.fetchone(cursor)
            if not end_time:
                continue
            break

        # Check values
        if not POSTGRESQL:
            # FIXME: Convert string to datetime for PostgreSQL
            logout_after = datetime_after()
            self.assert_(logout_before <= end_time <= logout_after)

class MysqlLogUser(MysqlLog):
    def testUserLogin(self):
        """
        User log in and logout:
        make sure that MySQL records login and then logout (and only once)
        with the right parameters.
        """

        # Delete old entries in MySQL user session table
        self.query("DELETE FROM %s WHERE start_time >= %s;" \
            % (DB_USER_TABLE, formatTimestamp(self.start_time)))

        sql = \
            "SELECT ip_saddr, user_id, username, " \
            "os_sysname, os_release, os_version, end_time " \
            "FROM %s WHERE start_time >= %s " \
            "ORDER BY start_time DESC;" % (DB_USER_TABLE, formatTimestamp(self.start_time))
        client = self._login(sql)
        self._logout(sql, client)

class MysqlLogPacket(MysqlLog):
    def setUp(self):
        self.iptables = Iptables()
        MysqlLog.setUp(self)

    def tearDown(self):
        MysqlLog.tearDown(self)
        self.iptables.flush()

    def testFilter(self):
        """
        User logs in, opens an authenticated connection, and
        closes the connection. Make sure that MySQL records the connection,
        only once, with the right parameters.
        """

        client = self.user.createClientWithCerts()
        time_before = int(time())
        timestamp_before = datetime_before()

        # Open allowed port
        testAllowPort(self, self.iptables, client)

        # Query DB
        if not POSTGRESQL:
            timestamp_field = "timestamp, "
        else:
            timestamp_field = ""
        sql = \
            "SELECT username, user_id, client_os, client_app, " \
            "tcp_dport, ip_saddr, ip_daddr, oob_time_sec, ip_protocol, " \
            "%sstart_timestamp, end_timestamp, oob_prefix " \
            "FROM %s WHERE oob_time_sec >= %s AND state=1;" \
            % (timestamp_field, DB_PACKET_TABLE, time_before)

        # Do the query
        for when in retry(timeout=QUERY_TIMEOUT):
            cursor = self.query(sql)
            if cursor.rowcount:
                break

        # Read result
        row = self.fetchone(cursor)
        timestamp_after = datetime_after()
        self.assertEqual(cursor.rowcount, 1)
        if POSTGRESQL:
            (username, user_id, client_os, client_app,
             tcp_dport, ip_saddr, ip_daddr, oob_time_sec, ip_protocol,
             start_timestamp, end_timestamp, oob_prefix) = row
        else:
            (username, user_id, client_os, client_app,
             tcp_dport, ip_saddr, ip_daddr, oob_time_sec, ip_protocol,
             timestamp, start_timestamp, end_timestamp, oob_prefix) = row

        # Check values
        self.assertEqual(username, client.username)
        self.assertEqual(user_id, self.user.uid)
        self.assertEqual(client_os, CLIENT_OS)
        self.assertEqual(client_app, CLIENT_APP)
        self.assertEqual(tcp_dport, VALID_PORT)
        self.assertEqual(IP(ip_saddr), client.ip)
        self.assert_(timestamp_before <= datetime.fromtimestamp(oob_time_sec) <= timestamp_after)
        if not POSTGRESQL:
            self.assert_(timestamp and timestamp_before <= timestamp <= timestamp_after)
        self.assertEqual(ip_protocol, 6)
        self.assertEqual(oob_prefix, OOB_PREFIX)
        # TODO: Check these timestamps
#        self.assertEqual(start_timestamp, ...)
#        self.assertEqual(end_timestamp, ...)

        # TODO: Open disallowed port
 #        testDisallowPort(self, self.iptables, client)

if __name__ == "__main__":
    print "Test nuauth module 'mysql' (log)"
    main()

