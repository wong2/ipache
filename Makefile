process: server.c 
	gcc -Wall -g server.c -o server `pkg-config --cflags gtk+-2.0` `pkg-config --libs gtk+-2.0`
