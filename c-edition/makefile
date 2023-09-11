.PHONY:prepare
prepare:
	gcc -c publish/db.c publish/socket.c -I. -lsqlite3 `pkg-config --cflags ctools-std` `pkg-config --libs ctools-std`
app:prepare
	gcc -o mini_im app.c *.o -lsqlite3 -I. `pkg-config --cflags ctools-std` `pkg-config --libs ctools-std`
clear:
	rm *.o
