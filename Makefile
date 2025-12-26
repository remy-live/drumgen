# Le nom du projet a changé
PROJECT = drumgen
CC      ?= gcc

# On inclut toujours nos librairies locales
CFLAGS  += -O3 -std=c99 -Wall -fPIC -I./lv2_lib/include

LDFLAGS += -shared -Wl,-Bsymbolic -lm

all: $(PROJECT).so

$(PROJECT).so: $(PROJECT).c
	$(CC) $(CFLAGS) $(PROJECT).c -o $@ $(LDFLAGS)

clean:
	rm -f $(PROJECT).so