CC=gcc
CFLAGS=-Wall `pkg-config fuse --cflags --libs` -lpthread -g
TARGET=CloudShare
SOURCE=src/main.c src/operator.c
OBJS=$(addsuffix .o, $(basename $(SOURCE)))
prepare:
	make clean
	
all: prepare $(TARGET)
$(TARGET): src/main.o src/operator.o
	$(CC) $(CFLAGS) src/main.c src/operator.c -o $@


operator.o:
	$(CC) $(CFLAGS) -c src/operator.c

main.o:
	$(CC) $(CFLAGS) -c src/main.c


clean:
	@echo clean
	rm -rf $(TARGET) *.o