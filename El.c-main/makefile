# =========================
# Arduino Uno (ATmega328P) - Windows GNU Make
# =========================

# Projekt
TARGET      := main
SRC         := main.c
BUILD_DIR   := build

# MCU / Clock
MCU         := atmega328p
F_CPU       := 16000000UL

# Programmer / Port
PROGRAMMER  := arduino
PORT        := COM3
BAUD        := 115200

# Verktyg (måste finnas i PATH)
CC          := avr-gcc
OBJCOPY     := avr-objcopy
SIZE        := avr-size
AVRDUDE     := avrdude

# Flags
CFLAGS      := -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -Wall -Wextra -std=c11
LDFLAGS     := -mmcu=$(MCU)

# Output-filer
ELF         := $(BUILD_DIR)/$(TARGET).elf
HEX         := $(BUILD_DIR)/$(TARGET).hex
MAP         := $(BUILD_DIR)/$(TARGET).map

# Objekt
OBJ         := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRC))

.PHONY: all clean flash size

all: $(HEX)

# Skapa build-katalog
$(BUILD_DIR):
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)

# Kompilera .c -> .o
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Länka .o -> .elf
$(ELF): $(OBJ)
	$(CC) $(LDFLAGS) $^ -Wl,-Map,$(MAP) -o $@
	$(SIZE) $@

# Skapa .hex från .elf
$(HEX): $(ELF)
	$(OBJCOPY) -O ihex -R .eeprom $< $@

size: $(ELF)

# Ladda upp till Arduino Uno
flash: $(HEX)
	$(AVRDUDE) -v -p $(MCU) -c $(PROGRAMMER) -P $(PORT) -b $(BAUD) -D -U flash:w:$(HEX):i

clean:
	@if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)