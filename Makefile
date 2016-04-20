include_dir = ./includes
src_dir = ./src
bin_dir = ./bin
CFLAGS = -std=c99 -D_XOPEN_SOURCE=700 -O0 -pthread -g

all: lab
utilis.o:
	gcc $(CFLAGS) -c -I$(include_dir) $(src_dir)/utilis.c -o $(bin_dir)/utilis.o
main.o:
	gcc $(CFLAGS) -c -I$(include_dir) $(src_dir)/main.c -o $(bin_dir)/main.o
common.o: utilis.o
	gcc $(CFLAGS) -c -I$(include_dir) $(src_dir)/common.c -o $(bin_dir)/common.o
lab: main.o common.o utilis.o
	gcc $(CFLAGS) -o $(bin_dir)/lab $(bin_dir)/main.o $(bin_dir)/common.o $(bin_dir)/utilis.o
clean:
	rm -rf $(bin_dir)/*.o
