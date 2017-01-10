/*
 * Sample libpurple program written by Michael C. Brook (http://libpurple.com/)
 * (Some fragments taken from libpurple nullclient.c example found at http://pidgin.im/)
 */

#include "purple.h"
#include "conversation.h"
#include "account.h"
#include "zmq.h"
#include "libmango.h"
#include "cJSON/cJSON.h"

#include <glib.h>

#include <signal.h>
#include <string.h>
#include <unistd.h>

#include <stdio.h>
#include <fcntl.h>

#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 

#define CUSTOM_USER_DIRECTORY  "/dev/null"
#define CUSTOM_PLUGIN_PATH     ""
#define PLUGIN_SAVE_PREF       "/purple/user/plugins/saved"
#define UI_ID                  "user"

struct m_node {
  char *version;
  char *node_id;
  const char **ports;
  int num_ports;
  char debug;
  char *server_addr;
  m_interface_t *interface;
  m_serialiser_t *serialiser;
  m_transport_t *local_gateway;
  m_dataflow_t *dataflow;
  void *zmq_context;
};

PurpleAccount *my_account;
m_node_t *mnode;

/**
 * The following eventloop functions are used in both pidgin and purple-text. If your
 * application uses glib mainloop, you can safely use this verbatim.
 */
#define PURPLE_GLIB_READ_COND  (G_IO_IN | G_IO_HUP | G_IO_ERR)
#define PURPLE_GLIB_WRITE_COND (G_IO_OUT | G_IO_HUP | G_IO_ERR | G_IO_NVAL)

typedef struct _PurpleGLibIOClosure{
  PurpleInputFunction function;
  guint result;
  gpointer data;
} PurpleGLibIOClosure;

typedef struct{
  PurpleAccountRequestType type;
  PurpleAccount *account;
  void *ui_handle;
  char *user;
  gpointer userdata;
  PurpleAccountRequestAuthorizationCb auth_cb;
  PurpleAccountRequestAuthorizationCb deny_cb;
  guint ref;
} PurpleAccountRequestInfo;

static void purple_glib_io_destroy(gpointer data){
  g_free(data);
}

static gboolean purple_glib_io_invoke(GIOChannel *source, GIOCondition condition, gpointer data){
  PurpleGLibIOClosure *closure = data;
  PurpleInputCondition purple_cond = 0;

  if(condition & PURPLE_GLIB_READ_COND)
    purple_cond |= PURPLE_INPUT_READ;
  if(condition & PURPLE_GLIB_WRITE_COND)
    purple_cond |= PURPLE_INPUT_WRITE;

  closure->function(closure->data, g_io_channel_unix_get_fd(source),
		    purple_cond);

  return TRUE;
}

static guint glib_input_add(gint fd, PurpleInputCondition condition, PurpleInputFunction function, gpointer data){
  PurpleGLibIOClosure *closure = g_new0(PurpleGLibIOClosure, 1);
  GIOChannel *channel;
  GIOCondition cond = 0;

  closure->function = function;
  closure->data = data;

  if(condition & PURPLE_INPUT_READ)
    cond |= PURPLE_GLIB_READ_COND;
  if(condition & PURPLE_INPUT_WRITE)
    cond |= PURPLE_GLIB_WRITE_COND;

  channel = g_io_channel_unix_new(fd);
  closure->result = g_io_add_watch_full(channel, G_PRIORITY_DEFAULT, cond,
					purple_glib_io_invoke, closure, purple_glib_io_destroy);

  g_io_channel_unref(channel);
  return closure->result;
}

static PurpleEventLoopUiOps glib_eventloops = {
    g_timeout_add,
    g_source_remove,
    glib_input_add,
    g_source_remove,
    NULL,
#if GLIB_CHECK_VERSION(2,14,0)
    g_timeout_add_seconds,
#else
    NULL,
#endif

    /* padding */
    NULL,
    NULL,
    NULL
  };
/*** End of the eventloop functions. ***/

static void network_disconnected(void){
  printf("This machine has been disconnected from the internet\n");
}

static void report_disconnect_reason(PurpleConnection *gc, PurpleConnectionError reason, const char *text){
  PurpleAccount *account = purple_connection_get_account(gc);
  printf("Connection disconnected: \"%s\" (%s)\n  >Error: %d\n  >Reason: %s\n", purple_account_get_username(account), purple_account_get_protocol_id(account), reason, text);
}

static PurpleConnectionUiOps connection_uiops = {
    NULL,                      /* connect_progress         */
    NULL,                      /* connected                */
    NULL,                      /* disconnected             */
    NULL,                      /* notice                   */
    NULL,                      /* report_disconnect        */
    NULL,                      /* network_connected        */
    network_disconnected,      /* network_disconnected     */
    report_disconnect_reason,  /* report_disconnect_reason */
    NULL,
    NULL,
    NULL
};

static void ui_init(void){
  /* This should initialize the UI components for all the modules. */
  purple_connections_set_ui_ops(&connection_uiops);
}

static PurpleCoreUiOps core_uiops = {
    NULL,
    NULL,
    ui_init,
    NULL,

    /* padding */
    NULL,
    NULL,
    NULL,
    NULL
};

static void init_libpurple(void){
  /* Set a custom user directory (optional) */
  purple_util_set_user_dir(CUSTOM_USER_DIRECTORY);

  /* We do not want any debugging for now to keep the noise to a minimum. */
  purple_debug_set_enabled(FALSE);

  /* Set the core-uiops, which is used to
   * 	- initialize the ui specific preferences.
   * 	- initialize the debug ui.
   * 	- initialize the ui components for all the modules.
   * 	- uninitialize the ui components for all the modules when the core terminates.
   */
  purple_core_set_ui_ops(&core_uiops);

  /* Set the uiops for the eventloop. If your client is glib-based, you can safely
   * copy this verbatim. */
  purple_eventloop_set_ui_ops(&glib_eventloops);

  /* Set path to search for plugins. The core (libpurple) takes care of loading the
   * core-plugins, which includes the protocol-plugins. So it is not essential to add
   * any path here, but it might be desired, especially for ui-specific plugins. */
  purple_plugins_add_search_path(CUSTOM_PLUGIN_PATH);

  /* Now that all the essential stuff has been set, let's try to init the core. It's
   * necessary to provide a non-NULL name for the current ui to the core. This name
   * is used by stuff that depends on this ui, for example the ui-specific plugins. */
  if (!purple_core_init(UI_ID)) {
    /* Initializing the core failed. Terminate. */
    fprintf(stderr, "libpurple initialization failed. Dumping core.\nPlease report this!\n");
    abort();
  }

  /* Create and load the buddylist. */
  purple_set_blist(purple_blist_new());
  purple_blist_load();

  /* Load the preferences. */
  purple_prefs_load();

  /* Load the desired plugins. The client should save the list of loaded plugins in
   * the preferences using purple_plugins_save_loaded(PLUGIN_SAVE_PREF) */
  purple_plugins_load_saved(PLUGIN_SAVE_PREF);

  /* Load the pounces. */
  purple_pounces_load();
}

static void signed_on(PurpleConnection *gc){
  PurpleAccount *account = purple_connection_get_account(gc);
  printf("Account connected: \"%s\" (%s)\n", purple_account_get_username(account), purple_account_get_protocol_id(account));
}

void try_to_rx(){
  void *sock = mnode->local_gateway->socket;
  int val;
  size_t len;
  int ret = zmq_getsockopt(sock, ZMQ_EVENTS, &val, &len);
  while(val & ZMQ_POLLIN){
    printf("%d %d %d %d %d\n", ret, val, len, ZMQ_POLLIN, ZMQ_POLLOUT);
    printf("TRYING\n");
    m_dataflow_recv(mnode->dataflow);
    ret = zmq_getsockopt(sock, ZMQ_EVENTS, &val, &len);
  }
}



static void received_im_msg(PurpleAccount *account, char *sender, char *message, PurpleConversation *conv, PurpleMessageFlags flags){
  if (conv==NULL){
    conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, sender);
  }

  if(mnode){
    printf("CONNECTED (%s) %s (%s): %s\n", purple_utf8_strftime("%H:%M:%S", NULL), sender, purple_conversation_get_name(conv), message);
    cJSON *im_recv_args = cJSON_CreateObject();
    cJSON_AddStringToObject(im_recv_args, "sender", sender);
    cJSON_AddStringToObject(im_recv_args, "conv", purple_conversation_get_name(conv));
    cJSON_AddStringToObject(im_recv_args, "message", message);
    m_node_send(mnode,"im_recv",im_recv_args,"stdio");
  }
  else{
    printf("NOT CONNECTED (%s) %s (%s): %s\n", purple_utf8_strftime("%H:%M:%S", NULL), sender, purple_conversation_get_name(conv), message);

  }
}

static void connect_to_signals(void){
  static int handle;
  purple_signal_connect(purple_connections_get_handle(), "signed-on", &handle, PURPLE_CALLBACK(signed_on), NULL);
  purple_signal_connect(purple_conversations_get_handle(), "received-im-msg", &handle, PURPLE_CALLBACK(received_im_msg), NULL);
}

typedef struct cred{
  char *un;
  char *pw;
} cred_t;

cred_t *read_creds(const char *fn){
  int len = 1000;
  char *un = malloc(len);
  int cred_file = open(fn, O_RDONLY);
  if(read(cred_file, un, len) < 0) exit(-1);
  close(cred_file);
  un[len-1] = 0;
  un[len-2] = '\n';
  un[len-3] = '\n';
  char *pw = un;
  while(*pw != '\n') pw++;
  *(pw++) = 0;
  char *pw_end = pw;
  while(*pw_end != '\n') pw_end++;
  *pw_end = 0;
  cred_t *c = malloc(sizeof(cred_t));
  c->un = strdup(un);
  c->pw = strdup(pw);
  free(un);
  return c;
}

void error(const char *msg)
{
    perror(msg);
    exit(0);
}

static gboolean gio_in(GIOChannel *gio, GIOCondition condition, gpointer data){
  GIOStatus ret;
  GError *err = NULL;
  gchar *msg;
  gsize len;
  
  if (condition & G_IO_HUP)
    g_error ("Read end of pipe died!\n");
  
  ret = g_io_channel_read_line (gio, &msg, &len, NULL, &err);
  if (ret == G_IO_STATUS_ERROR)
    g_error ("Error reading: %s\n", err->message);
  
  printf ("Read %u bytes: %s\n", len, msg);
  
  g_free (msg);
  
  return TRUE;
}

int connect_sock(const char *hostname, int port){
    int sockfd, portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    char buffer[256];
    portno = port;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");
    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);
    serv_addr.sin_port = htons(portno);
    if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) 
        error("ERROR connecting");

    return sockfd;
    bzero(buffer,256);
    n = read(sockfd,buffer,255);
    if (n < 0) 
         error("ERROR reading from socket");
    printf("%s\n",buffer);
    close(sockfd);
    return 0;
}

void init_sock(){
  GIOChannel *gio;
  int fd = connect_sock("localhost", 1919);
  gio = g_io_channel_unix_new (fd);
  if (!gio)
    g_error ("Cannot create new GIOChannel!\n");
  
  if (!g_io_add_watch (gio, G_IO_IN | G_IO_HUP, gio_in, NULL))
    g_error ("Cannot add watch on GIOChannel!\n");
}


static gboolean gio_mango_in(GIOChannel *gio, GIOCondition condition, gpointer data){
  printf("SOMETHING IN\n");

  if (condition & G_IO_HUP)
    g_error ("Read end of pipe died!\n");

  try_to_rx();

  return TRUE;
}

cJSON *excite(m_node_t *node, cJSON *header, cJSON *args){
  cJSON *ans = cJSON_CreateObject();
  char *s = cJSON_GetObjectItem(args,"str")->valuestring;
  unsigned long l = strlen(s);
  char *excited = malloc(l+2);
  printf("%s!\n",cJSON_GetObjectItem(args,"str")->valuestring);
  sprintf(excited, "%s!",cJSON_GetObjectItem(args,"str")->valuestring);
  cJSON_AddStringToObject(ans,"excited",excited);
  return ans;
}

cJSON *m_im_send(m_node_t *node, cJSON *header, cJSON *args){
  cJSON *ans = cJSON_CreateObject();
  char *name = cJSON_GetObjectItem(args,"name")->valuestring;
  char *msg = cJSON_GetObjectItem(args,"message")->valuestring;

  PurpleAccount *account = my_account;
  PurpleConversation *conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, name);
  PurpleConvIm *im_data = purple_conversation_get_im_data(conv);
  printf("im_send %s to %s\n", msg, name);
  purple_conv_im_send(im_data, msg);
}

int connect_mango(){
  setenv("MANGO_ID","jam",1);
  setenv("MC_ADDR","tcp://localhost:1212",1);
  printf("Connecting...\n");
  mnode = m_node_new(0);
  m_node_add_interface(mnode, "./jam.yaml");
  m_node_handle(mnode, "im_send", m_im_send);
  m_node_handle(mnode, "excite", excite);
  void *sock = mnode->local_gateway->socket;
  int val;
  size_t len = sizeof(val);
  int ret = zmq_getsockopt(sock, ZMQ_FD, &val, &len);
  printf("%d %d\n", ret, val);
  try_to_rx();
  printf("Connected!\n");
  return val;
}

void init_mango(){
  GIOChannel *gio;
  int fd = connect_mango();
  gio = g_io_channel_unix_new(fd);
  if (!gio)
    g_error ("Cannot create new GIOChannel!\n");
  
  if (!g_io_add_watch (gio, G_IO_IN | G_IO_ERR | G_IO_HUP, gio_mango_in, NULL))
    g_error ("Cannot add watch on GIOChannel!\n");
  
  m_node_ready(mnode);
  try_to_rx();
}

int main(int argc, char *argv[]){
  GMainLoop *loop = g_main_loop_new(NULL, FALSE);

  /* libpurple's built-in DNS resolution forks processes to perform
   * blocking lookups without blocking the main process.  It does not
   * handle SIGCHLD itself, so if the UI does not you quickly get an army
   * of zombie subprocesses marching around.
   */
  signal(SIGCHLD, SIG_IGN);

  init_libpurple();

  printf("libpurple initialized. Running version %s.\n", purple_core_get_version()); //I like to see the version number

  cred_t *c = read_creds("./.creds");

  init_mango();
  connect_to_signals();

  my_account = purple_account_new(c->un, "prpl-jabber"); //this could be prpl-aim, prpl-yahoo, prpl-msn, prpl-icq, etc.
  purple_account_set_password(my_account, c->pw);

  purple_accounts_add(my_account);
  purple_account_set_enabled(my_account, UI_ID, TRUE);

  g_main_loop_run(loop);

  free(c->un);
  free(c->pw);
  free(c);
  return 0;
}
