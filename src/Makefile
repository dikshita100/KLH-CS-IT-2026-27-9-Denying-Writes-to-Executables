
CC = gcc
CFLAGS = -Wall -Wextra
GTK_FLAGS = $(shell pkg-config --cflags --libs gtk+-3.0)

TARGET = executable_protection
SOURCE = executable_protection_gui.c

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $(SOURCE) -o $(TARGET) $(GTK_FLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) protection.log
