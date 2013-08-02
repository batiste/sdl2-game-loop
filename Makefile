CFLAGS =
SDLFLAGS = $(shell SDL2/sdl2-x86/bin/sdl2-config --libs --cflags) -lSDL2_image -lSDL2_ttf -lSDL2_mixer
MXMLFLAGS = -L$(shell pwd)/mxml/mxml-bin/lib/ -Wl,-rpath,$(shell pwd)/mxml/mxml-bin/lib/  -I$(shell pwd)/mxml/mxml-bin/include/ -lmxml
CC=gcc -Wall

# -lXi -lXmu

all:	glbuild

glbuild:
	$(CC) $(CFLAGS) -o glapp main.c $(SDLFLAGS) $(MXMLFLAGS)

eglbuild:
	$(CC) $(CFLAGS) -o glapp main.c -lGLESv2 $(SDLFLAGS) $(MXMLFLAGS)

debug:
	$(CC) $(CFLAGS) -o glapp main.c $(SDLFLAGS) $(MXMLFLAGS) -g -O0

clean:
	rm glapp eglapp debugapp
