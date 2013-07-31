CFLAGS =
SDLFLAGS = $(shell SDL2/sdl2-x86/bin/sdl2-config --libs --cflags) -lSDL2_image -lSDL2_ttf -lSDL2_mixer
CC=gcc -Wall

# -lXi -lXmu

all:	glbuild

glbuild:
	$(CC) $(CFLAGS) -o glapp main.c $(SDLFLAGS)

eglbuild:
	$(CC) $(CFLAGS) -o glapp main.c -lGLESv2 $(SDLFLAGS)

debug:
	$(CC) $(CFLAGS) -o glapp main.c $(SDLFLAGS) -g -O0

clean:
	rm glapp eglapp debugapp
