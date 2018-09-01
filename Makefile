PROG ?= simulator
SSL_FLAGS ?= -D MG_ENABLE_SSL -DMG_SSL_IF=MG_SSL_IF_MBEDTLS -lmbedtls -lmbedcrypto -lmbedx509
CFLAGS ?= -W -Wall -g -O2 -D MG_ENABLE_CALLBACK_USERDATA=1

all: run

$(PROG): main.c mongoose.c
	$(CC) $(CFLAGS) $(SSL_FLAGS) $? -o $@

run: $(PROG)
	./$(PROG) $(ARGS)

clean:
	rm -rf $(PROG) $(PROG).dSYM