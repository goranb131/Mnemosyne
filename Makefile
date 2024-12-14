# Variables
CC = cc
CFLAGS = -Wall -Wextra -O2 -std=c99
TARGET = mnemos
PREFIX = /usr/local

# Default target: build
all: $(TARGET)

# Compile the tool
$(TARGET): mnemos.c
	$(CC) $(CFLAGS) $< -o $@

# Install the binary
install: $(TARGET)
	mkdir -p $(PREFIX)/bin
	cp $(TARGET) $(PREFIX)/bin
	chmod 755 $(PREFIX)/bin/$(TARGET)
	@echo "Installed $(TARGET) to $(PREFIX)/bin"

# Uninstall the binary
uninstall:
	rm -f $(PREFIX)/bin/$(TARGET)
	@echo "Uninstalled $(TARGET) from $(PREFIX)/bin"

# Clean up build files
clean:
	rm -f $(TARGET)
	@echo "Cleaned up build files"

# Help target
help:
	@echo "Usage:"
	@echo "  make          Build the mnemos tool"
	@echo "  make install  Install the mnemos tool to $(PREFIX)/bin"
	@echo "  make uninstall Remove the mnemos tool from $(PREFIX)/bin"
	@echo "  make clean    Remove build files"