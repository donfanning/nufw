// DEBUG_LEVEL's value is 1 to 8

#define DEBUG_LEVEL_FATAL		1
#define DEBUG_LEVEL_CRITICAL		2
#define DEBUG_LEVEL_SERIOUS_WARNING	3
#define DEBUG_LEVEL_WARNING		4
#define DEBUG_LEVEL_SERIOUS_MESSAGE	5
#define DEBUG_LEVEL_MESSAGE		6
#define DEBUG_LEVEL_INFO		7
#define DEBUG_LEVEL_DEBUG		8
#define DEBUG_LEVEL_VERBOSE_DEBUG	9

#define DEFAULT_DEBUG_LEVEL		DEBUG_LEVEL_SERIOUS_WARNING

#define MIN_DEBUG_LEVEL			DEBUG_LEVEL_CRITICAL
#define MAX_DEBUG_LEVEL			DEBUG_LEVEL_VERBOSE_DEBUG

#define DEBUG_AREA_MAIN		1
#define DEBUG_AREA_PACKET	2
#define DEBUG_AREA_USER		4
#define DEBUG_AREA_GW		8
#define DEBUG_AREA_AUTH		16

/* Default is to debug all*/
#define DEFAULT_DEBUG_AREAS DEBUG_AREA_MAIN||DEBUG_AREA_PACKET||DEBUG_AREA_USER||DEBUG_AREA_GW||DEBUG_AREA_AUTH

#define LOG_FACILITY "LOG_DAEMON"

#define DEBUG_OR_NOT(LOGLEVEL,LOGAREA) (LOGAREA&&debug_areas)&&(debug_level>=LOGLEVEL)

#include <glib.h>

int set_glib_loghandlers();
void process_g_message (const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data);
void process_g_fatal (const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data);
