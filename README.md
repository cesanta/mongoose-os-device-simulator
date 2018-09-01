# Mongoose OS devie simulator

This Mac/Linux/Windows program can be used to simulate a real IoT
device for
[device management dashboard](https://mongoose-os.com/docs/userguide/dashboard.md).

Usage:

```
$ make
cc -W -Wall -g -O2 -D MG_ENABLE_CALLBACK_USERDATA=1 -D MG_ENABLE_SSL -DMG_SSL_IF=MG_SSL_IF_MBEDTLS -lmbedtls -lmbedcrypto -lmbedx509 main.c mongoose.c -o simulator
./simulator
Enter access token: xxxxxxxxxxxxxxx
reconnecting to wss://dash.mongoose-os.com/api/v2/rpc
WS <-: 28 [{"id":1,"method":"RPC.List"}]
WS ->: 103 [{"id":1,"result":["FS.Put","FS.Get","FS.Rename","FS.Remove","FS.List","RPC.List","Sys.GetInfo","echo"]}]
WS <-: 27 [{"id":2,"method":"FS.List"}]
```