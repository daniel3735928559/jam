gcc `pkg-config --libs glib-2.0` -lzmq -L ../../libmango/c/ -I ../../libmango/c -I /usr/include/libpurple/ -I /usr/include/glib-2.0/ -I /usr/lib/glib-2.0/include/ -lmango -lyaml -lpurple jam.c -o jam
