all:
	gcc -std=c99 main.c -o main -I./include -L./lib  -lavformat -lavcodec -lswscale -lavdevice -lavfilter -lswresample -lavutil -lpthread -lm -lasound -lrt -lz
