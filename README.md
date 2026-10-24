# Game skeleton

A small 2D game loop written in C with [SDL3](https://libsdl.org). It loads and
displays a TMX map made with the [Tiled](https://www.mapeditor.org) editor, and
lets you walk a character around it, with collisions, animations and sound.

![Screenshot](assets/shot.png)

## Features

- SDL3 main callbacks (`SDL_AppInit` / `SDL_AppIterate` / ...)
- Fixed-timestep physics (50 Hz) with interpolated rendering, so the game runs
  at the same speed with vsync on or with an uncapped frame rate
- Fixed logical resolution (800×600), letterboxed into a resizable,
  HiDPI-aware window with crisp pixel-art scaling
- TMX reader: embedded tilesets (with margin/spacing), tile layers in XML or
  CSV encoding, object groups and object properties
- Camera that follows the character and stays inside the map
- Only the visible tiles are drawn

## Controls

| Key                 | Action                          |
| ------------------- | ------------------------------- |
| W A S D / arrows    | Move (physical keys, so it works on AZERTY too) |
| Space               | Sword                           |
| C                   | Toggle vsync / uncapped FPS     |
| Esc                 | Quit                            |

Any key skips the splash screen.

## Building

Requirements: a C17 compiler, CMake ≥ 3.24, pkg-config, SDL3, SDL3_image,
SDL3_ttf, SDL3_mixer (3.x) and [Mini-XML](https://www.msweet.org/mxml/) 4.

### macOS

```sh
brew install cmake pkg-config sdl3 sdl3_image sdl3_ttf sdl3_mixer libmxml
cmake -S . -B build
cmake --build build
./build/game
```

### Linux

Install the same libraries from your distribution (or build SDL3 from source),
then use the same CMake commands.

### Debugging

```sh
cmake -S . -B build-asan -DENABLE_SANITIZERS=ON
cmake --build build-asan
./build-asan/game
```

Assets are copied next to the executable at build time and located with
`SDL_GetBasePath()`, so the game can be launched from any directory.

## Tilesets

`assets/ground.png` and `assets/tiles.png` are the original hand-drawn tiles.
`assets/nature.png` (water, path, bushes, rocks, flowers, fences...) is
generated in the same style by `tools/gen_nature_tiles.py`; edit the script and
re-run it with Pillow installed (`pip install pillow`) to tweak or add tiles.
The tile layout is documented at the top of the script.

## Layout

| File           | Purpose                                              |
| -------------- | ---------------------------------------------------- |
| `src/main.c`   | Game state, update loop and rendering                |
| `src/engine.*` | Window, renderer, audio, asset loading and cleanup   |
| `src/sprite.*` | Sprites and sprite-sheet animations                  |
| `src/tmx.*`    | TMX map parser                                       |
