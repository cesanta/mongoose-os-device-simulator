#include "mjson.h"

/* Common JSON-RPC error codes */
#define JSONRPC_ERROR_INVALID -32700    /* Invalid JSON was received */
#define JSONRPC_ERROR_NOT_FOUND -32601  /* The method does not exist */
#define JSONRPC_ERROR_BAD_PARAMS -32602 /* Invalid params passed */
#define JSONRPC_ERROR_INTERNAL -32603   /* Internal JSON-RPC error */

struct jsonrpc_method {
  const char *method;
  int method_sz;
  int (*cb)(char *, int, struct mjson_out *, void *);
  void *cbdata;
  struct jsonrpc_method *next;
};

/*
 * Main Freshen context, stores current request information and a list of
 * exported RPC methods.
 */
struct jsonrpc_ctx {
  struct jsonrpc_method *methods;
  void *privdata;
  int (*sender)(char *buf, int len, void *privdata);
};

#define JSONRPC_CTX_INTIALIZER \
  { NULL, NULL, NULL }

/* Registers function fn under the given name within the given RPC context */
#define jsonrpc_ctx_export(ctx, name, fn, ud)                               \
  do {                                                                      \
    static struct jsonrpc_method m = {(name), sizeof(name) - 1, (fn), NULL, \
                                      NULL};                                \
    m.cbdata = (ud);                                                        \
    m.next = (ctx)->methods;                                                \
    (ctx)->methods = &m;                                                    \
  } while (0)

int jsonrpc_ctx_notify(struct jsonrpc_ctx *ctx, char *buf, int len) {
  return ctx->sender == NULL ? 0 : ctx->sender(buf, len, ctx->privdata);
}

static struct jsonrpc_ctx jsonrpc_default_context = JSONRPC_CTX_INTIALIZER;

#define jsonrpc_export(name, fn, ud) \
  jsonrpc_ctx_export(&jsonrpc_default_context, (name), (fn), (ud))

#define jsonrpc_notify(buf, len) \
  jsonrpc_ctx_notify(&jsonrpc_default_context, (buf), (len))

#define jsonrpc_loop(version, pass)                                          \
  jsonrpc_poll(&jsonrpc_default_context, "wss://dash.freshen.cc/api/v2/rpc", \
               (version), (pass))

static int jsonrpc_ctx_process(struct jsonrpc_ctx *ctx, char *req, int req_sz) {
  const char *id = NULL, *params = NULL;
  char method[50];
  int id_sz = 0, method_sz = 0, params_sz = 0, code = JSONRPC_ERROR_NOT_FOUND;
  struct jsonrpc_method *m;

  /* Method must exist and must be a string. */
  if ((method_sz = mjson_find_string(req, req_sz, "$.method", method,
                                     sizeof(method))) <= 0) {
    return JSONRPC_ERROR_INVALID;
  }

  /* id and params are optional. */
  mjson_find(req, req_sz, "$.id", &id, &id_sz);
  mjson_find(req, req_sz, "$.params", &params, &params_sz);

  char *res = NULL, *frame = NULL;
  struct mjson_out rout = MJSON_OUT_DYNAMIC_BUF(&res);
  struct mjson_out fout = MJSON_OUT_DYNAMIC_BUF(&frame);

  for (m = ctx->methods; m != NULL; m = m->next) {
    if (m->method_sz == method_sz && !memcmp(m->method, method, method_sz)) {
      if (params == NULL) params = "";
      int code = m->cb((char *) params, params_sz, &rout, m->cbdata);
      if (id == NULL) {
        /* No id, not sending any reply. */
        free(res);
        return code;
      } else if (code == 0) {
        mjson_printf(&fout, "{%Q:%.*s,%Q:%s}", "id", id_sz, id, "result",
                     res == NULL ? "null" : res);
      } else {
        mjson_printf(&fout, "{%Q:%.*s,%Q:{%Q:%d,%Q:%s}}", "id", id_sz, id,
                     "error", "code", code, "message",
                     res == NULL ? "null" : res);
      }
      break;
    }
  }
  if (m == NULL) {
    mjson_printf(&fout, "{%Q:%.*s,%Q:{%Q:%d,%Q:%Q}}", "id", id_sz, id, "error",
                 "code", code, "message", "method not found");
  }
  ctx->sender(frame, strlen(frame), ctx->privdata);
  free(frame);
  free(res);
  return code;
}

#if defined(__APPLE__) || defined(__linux__)
#define JSONRPC_FS_ENABLE
#endif

static int info(char *args, int len, struct mjson_out *out, void *userdata) {
#if defined(__APPLE__)
  const char *arch = "darwin";
#elif defined(__linux__)
  const char *arch = "linux";
#elif defined(ESP_PLATFORM)
  const char *arch = "esp32";
#elif defined(ESP8266) || defined(MG_ESP8266)
  const char *arch = "esp8266";
#elif defined(MBED_LIBRARY_VERSION)
  const char *arch = "mbedOS";
#else
  const char *arch = "unknown";
#endif

#if !defined(JSONRPC_APP)
#define JSONRPC_APP "posix_device"
#endif

  (void) args;
  (void) len;
  mjson_printf(out, "{%Q:%Q, %Q:%Q, %Q:%Q, %Q:%Q}", "fw_version", userdata,
               "arch", arch, "fw_id", __DATE__ " " __TIME__, "app",
               JSONRPC_APP);
  return 0;
}

static int rpclist(char *in, int in_len, struct mjson_out *out, void *ud) {
  struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *) ud;
  mjson_print_buf(out, "[", 1);
  for (struct jsonrpc_method *m = ctx->methods; m != NULL; m = m->next) {
    if (m != ctx->methods) mjson_print_buf(out, ",", 1);
    mjson_print_str(out, m->method, strlen(m->method));
  }
  mjson_print_buf(out, "]", 1);
  (void) in;
  (void) in_len;
  return 0;
}

#if defined(JSONRPC_OTA_ENABLE)
#include <stdio.h>
#include <stdlib.h>
/*
 * Common OTA code
 */
static int jsonrpc_rpc_ota_begin(char *in, int in_len, struct mjson_out *out,
                                 void *userdata) {
  struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *) userdata;
  int r = jsonrpc_ota_begin(ctx);
  mjson_printf(out, "%s", r == 0 ? "true" : "false");
  return r;
  (void) in;
  (void) in_len;
}

static int jsonrpc_rpc_ota_end(char *in, int in_len, struct mjson_out *out,
                               void *userdata) {
  struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *) userdata;
  int success = mjson_find_number(in, in_len, "$.success", -1);
  if (success < 0) {
    mjson_printf(out, "%Q", "bad args");
    return JSONRPC_ERROR_BAD_PARAMS;
  } else if (jsonrpc_ota_end(ctx, success) != 0) {
    mjson_printf(out, "%Q", "failed");
    return 500;
  } else {
    mjson_printf(out, "%s", "true");
    return 0;
  }
}

static int jsonrpc_rpc_ota_write(char *in, int len, struct mjson_out *out,
                                 void *userdata) {
  struct jsonrpc_ctx *ctx = (struct jsonrpc_ctx *) userdata;
  char *p;
  int n, result = 0;
  if (mjson_find(in, len, "$", (const char **) &p, &n) != MJSON_TOK_STRING) {
    mjson_printf(out, "%Q", "expecting base64 encoded data");
    result = JSONRPC_ERROR_BAD_PARAMS;
  } else {
    int dec_len = mjson_base64_dec(p, n, p, n);
    if (jsonrpc_ota_write(ctx, p, dec_len) != 0) {
      mjson_printf(out, "%Q", "write failed");
      result = 500;
    } else {
      mjson_printf(out, "%s", "true");
    }
  }
  return result;
}
#endif

#if defined(JSONRPC_FS_ENABLE)
#include <dirent.h>
static int fslist(char *in, int in_len, struct mjson_out *out, void *ud) {
  DIR *dirp;
  mjson_print_buf(out, "[", 1);
  if ((dirp = opendir(".")) != NULL) {
    struct dirent *dp;
    int i = 0;
    while ((dp = readdir(dirp)) != NULL) {
      /* Do not show current and parent dirs */
      if (strcmp((const char *) dp->d_name, ".") == 0 ||
          strcmp((const char *) dp->d_name, "..") == 0) {
        continue;
      }
      if (i > 0) mjson_print_buf(out, ",", 1);
      mjson_print_str(out, dp->d_name, strlen(dp->d_name));
      i++;
    }
    closedir(dirp);
  }
  mjson_print_buf(out, "]", 1);
  (void) ud;
  (void) in;
  (void) in_len;
  return 0;
}

static int fsremove(char *in, int in_len, struct mjson_out *out, void *ud) {
  char fname[50];
  int result = 0;
  if (mjson_find_string(in, in_len, "$.filename", fname, sizeof(fname)) <= 0) {
    mjson_printf(out, "%Q", "filename is missing");
    result = JSONRPC_ERROR_BAD_PARAMS;
  } else if (remove(fname) != 0) {
    mjson_printf(out, "%Q", "remove() failed");
    result = -1;
  } else {
    mjson_printf(out, "%s", "true");
  }
  (void) ud;
  return result;
}

static int fsrename(char *in, int in_len, struct mjson_out *out, void *ud) {
  char src[50], dst[50];
  int result = 0;
  if (mjson_find_string(in, in_len, "$.src", src, sizeof(src)) <= 0 ||
      mjson_find_string(in, in_len, "$.dst", dst, sizeof(dst)) <= 0) {
    mjson_printf(out, "%Q", "src and dst are required");
    result = JSONRPC_ERROR_BAD_PARAMS;
  } else if (rename(src, dst) != 0) {
    mjson_printf(out, "%Q", "rename() failed");
    result = -1;
  } else {
    mjson_printf(out, "%s", "true");
  }
  (void) ud;
  return result;
}

static int fsget(char *in, int in_len, struct mjson_out *out, void *ud) {
  char fname[50], *chunk = NULL;
  int offset = mjson_find_number(in, in_len, "$.offset", 0);
  int len = mjson_find_number(in, in_len, "$.len", 512);
  int result = 0;
  FILE *fp = NULL;
  if (mjson_find_string(in, in_len, "$.filename", fname, sizeof(fname)) <= 0) {
    mjson_printf(out, "%Q", "filename is required");
    result = JSONRPC_ERROR_BAD_PARAMS;
  } else if ((chunk = malloc(len)) == NULL) {
    mjson_printf(out, "%Q", "chunk alloc failed");
    result = -1;
  } else if ((fp = fopen(fname, "rb")) == NULL) {
    mjson_printf(out, "%Q", "fopen failed");
    result = -2;
  } else {
    fseek(fp, offset, SEEK_SET);
    int n = fread(chunk, 1, len, fp);
    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);
    mjson_printf(out, "{%Q:%V,%Q:%d}", "data", n, chunk, "left",
                 size - (n + offset));
  }
  if (chunk != NULL) free(chunk);
  if (fp != NULL) fclose(fp);
  (void) ud;
  return result;
}

static int fsput(char *in, int in_len, struct mjson_out *out, void *ud) {
  char fname[50], *data = NULL;
  FILE *fp = NULL;
  int n, result = 0;
  int append = mjson_find_bool(in, in_len, "$.append", 0);
  if (mjson_find(in, in_len, "$.data", (const char **) &data, &n) !=
          MJSON_TOK_STRING ||
      mjson_find_string(in, in_len, "$.filename", fname, sizeof(fname)) <= 0) {
    mjson_printf(out, "%Q", "data and filename are required");
    result = JSONRPC_ERROR_BAD_PARAMS;
  } else if ((fp = fopen(fname, append ? "ab" : "wb")) == NULL) {
    mjson_printf(out, "%Q", "fopen failed");
    result = 500;
  } else {
    /* Decode in-place */
    int dec_len = mjson_base64_dec(data + 1, n - 2, data, n);
    if ((int) fwrite(data, 1, dec_len, fp) != dec_len) {
      mjson_printf(out, "%Q", "write failed");
      result = 500;
    } else {
      mjson_printf(out, "{%Q:%d}", "written", dec_len);
    }
  }
  if (fp != NULL) fclose(fp);
  (void) ud;
  return result;
}
#endif

static void jsonrpc_rpc_init(struct jsonrpc_ctx *ctx, const char *version) {
  jsonrpc_ctx_export(ctx, "Sys.GetInfo", info, (void *) version);
  jsonrpc_ctx_export(ctx, "RPC.List", rpclist, ctx);

#if defined(JSONRPC_FS_ENABLE)
  jsonrpc_ctx_export(ctx, "FS.List", fslist, ctx);
  jsonrpc_ctx_export(ctx, "FS.Remove", fsremove, ctx);
  jsonrpc_ctx_export(ctx, "FS.Rename", fsrename, ctx);
  jsonrpc_ctx_export(ctx, "FS.Get", fsget, ctx);
  jsonrpc_ctx_export(ctx, "FS.Put", fsput, ctx);
#endif
}
