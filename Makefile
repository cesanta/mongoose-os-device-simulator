PROG ?= simulator

all: run

$(PROG): main.c mongoose.c
	$(CC) -W -Wall -g -O2 -D MG_ENABLE_CALLBACK_USERDATA=1 $? -o $@

run: $(PROG)
	./$(PROG) $(ARGS)

clean:
	rm -rf $(PROG) $(PROG).dSYM