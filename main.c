#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "mjson.c"
#include "mongoose.h"

#define PROTO "dash.mongoose-os.com"
#define ORIGIN "Origin: https://mongoose-os.com\r\n"
#define RECONNECTION_INTERVAL_SECONDS 3.0

static const char *url = NULL, *version = NULL, *pass = NULL,
                  *mac = "aabbccddeeff";

struct privdata {
  struct mg_mgr mgr;
  struct mg_connection *c;
  double disconnection_time;
  struct jsonrpc_ctx *ctx;
};

static void ws_handler(struct mg_connection *c, int ev, void *arg, void *ud) {
  struct privdata *pd = (struct privdata *) ud;
  // printf("%s %p %d %p %p\n", __func__, c, ev, arg, ud);
  switch (ev) {
    case MG_EV_WEBSOCKET_HANDSHAKE_DONE:
      // Successfully connected. Clear off rollback timer and commit
      mg_set_timer(c, 0);
      break;
    case MG_EV_WEBSOCKET_FRAME: {
      struct websocket_message *wm = arg;
      int len = wm->size;
      while (len > 0 && wm->data[len - 1] != '}') len--;
      printf("WS <-: %d [%.*s]\n", len, len, (char *) wm->data);
      jsonrpc_ctx_process(pd->ctx, (char *) wm->data, len);
      break;
    }
    case MG_EV_CLOSE:
      printf("%s\n", "disconnected");
      mg_set_timer(c, 0);
      c->flags |= MG_F_CLOSE_IMMEDIATELY;
      pd->c = NULL;
      pd->disconnection_time = mg_time();
      break;
  }
}

static void reconnect(struct privdata *pd, const char *url, const char *token) {
  char buf[200];
  printf("reconnecting to %s\n", url);
  snprintf(buf, sizeof(buf), "Authorization: Bearer %s\r\n" ORIGIN, token);
  pd->c = mg_connect_ws(&pd->mgr, ws_handler, pd, url, PROTO, buf);
}

static int ws_sender(const char *buf, int len, void *privdata) {
  struct privdata *pd = (struct privdata *) privdata;
  printf("WS ->: %d [%.*s]\n", len, len, buf);
  mg_send_websocket_frame(pd->c, WEBSOCKET_OP_TEXT, buf, len);
  return len;
}

static void poll(struct jsonrpc_ctx *ctx, const char *url, const char *token) {
  struct privdata *pd = (struct privdata *) ctx->userdata;
  // printf("pollin %p %p...\n", pd, pd ? pd->c : NULL);
  if (pd == NULL) {
    pd = (struct privdata *) calloc(1, sizeof(*pd));
#if defined(__unix__)
    signal(SIGPIPE, SIG_IGN);
#endif
    pd->ctx = ctx;
    mg_mgr_init(&pd->mgr, pd);
    jsonrpc_init(ws_sender, NULL, pd);
  }
  if (pd == NULL) {
    printf("OOM %d bytes\n", (int) sizeof(*pd));
  } else if (pd->c == NULL) {
    reconnect(pd, url, token);
  } else {
    mg_mgr_poll(&pd->mgr, 10);
  }
}

int mg_ssl_if_mbed_random(void *ctx, unsigned char *buf, size_t len) {
  while (len-- > 0) {
    *buf++ = (unsigned char) rand();
  }
  (void) ctx;
  return 0;
}

// Shadow.Delta callback. Sets reported = desired.
static void delta_cb(struct jsonrpc_request *r) {
  const char *p = NULL;
  int len = 0;
  mjson_find(r->params, r->params_len, "$.state", &p, &len);
  if (p != NULL && len > 0) {
    jsonrpc_return_success(r, "%.*s", len, p);
  } else {
    jsonrpc_return_error(r, JSONRPC_ERROR_BAD_PARAMS, "%Q", "invalid params");
  }
}

static void config_get_cb(struct jsonrpc_request *r) {
  jsonrpc_return_success(r, "{%Q:{%Q:%Q}}", "dash", "token", pass);
}

static void info_cb(struct jsonrpc_request *r) {
  jsonrpc_return_success(r, "{%Q:%Q, %Q:%Q, %Q:%Q, %Q:%Q, %Q:%Q}", "fw_version",
                         version, "arch", "posix", "fw_id",
                         __DATE__ " " __TIME__, "app", "simulator", "mac", mac);
}

static char *prompt(char *buf, size_t len, const char *msg) {
  printf("%s ", msg);
  fflush(stdout);
  buf[0] = '\0';
  fgets(buf, len, stdin);
  buf[len - 1] = '\0';
  int n = strlen(buf) - 1;
  while (n >= 0 && isspace(buf[n])) buf[n--] = '\0';
  return buf;
}

int main(int argc, char *argv[]) {
  char buf[100];

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-b") == 0) {
      url = argv[++i];
    } else if (strcmp(argv[i], "-v") == 0) {
      version = argv[++i];
    } else if (strcmp(argv[i], "-m") == 0) {
      mac = argv[++i];
    } else if (strcmp(argv[i], "-t") == 0) {
      pass = argv[++i];
    }
  }

  if (url == NULL) url = "wss://dash.mongoose-os.com/api/v2/rpc";
  if (version == NULL) version = "1.0.0";
  if (pass == NULL) pass = prompt(buf, sizeof(buf), "Enter access token:");

  jsonrpc_export("Shadow.Delta", delta_cb, NULL);
  jsonrpc_export("Config.Get", config_get_cb, NULL);
  jsonrpc_export("Sys.GetInfo", info_cb, NULL);
  for (;;) poll(&jsonrpc_default_context, url, pass);

  return EXIT_SUCCESS;
}
