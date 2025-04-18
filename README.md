# NinjaFruit Game in C with SDL

A Fruit Ninja style game built in C, using SDL for graphics and audio. This implementation focuses on OS concepts including threading, forking, and pipes.

## Features

- **SDL Graphics**: High-quality fruit and bomb images with rotation and slicing effects
- **SDL Audio**: Background music and sound effects for slicing fruits and bombs
- **Threading**: Spawns fruits in a separate thread to demonstrate thread synchronization
- **Forking**: Uses a child process to manage power-ups
- **Pipes**: Communication between parent and child processes for power-up events
- **Mutex Locks**: Protects the shared game objects data structure
- **Signal Handling**: Handles SIGINT for graceful termination and score saving

## Game Mechanics

- Fruits and bombs are thrown from the bottom of the screen
- Player slices fruits by dragging the mouse
- Slicing fruits increases score, slicing bombs decreases score
- Missing fruits decreases score
- Power-ups are occasionally spawned by the child process

## Prerequisites

You need to install SDL2, SDL2_image, and SDL2_mixer libraries:

### On macOS (using Homebrew):

```bash
brew install sdl2 sdl2_image sdl2_mixer
```

### On Ubuntu/Debian:

```bash
sudo apt-get install libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev
```

### On Fedora:

```bash
sudo dnf install SDL2-devel SDL2_image-devel SDL2_mixer-devel
```

## Building and Running

```bash
# Compile the game
make

# Run the game
./ninja_fruit

# To exit the game, close the window, press Escape, or press Ctrl+C
```

## Controls

- Drag the mouse to slice fruits
- Avoid slicing bombs (black objects with a fuse)
- Press Escape or close the window to exit

## Assets

The game uses the following assets:

- **Images**: High-quality PNG images for apples, bananas, oranges, and bombs
- **Sounds**:
  - Slice sound when cutting fruits
  - Explosion sound when cutting bombs
  - Background music

## OS Concepts Demonstrated

1. **Threading**

   - A separate thread handles fruit spawning (pthread_create)
   - Thread synchronization with mutex locks (pthread_mutex_lock/unlock)

2. **Process Creation**

   - Fork() to create a child process for power-ups
   - Wait() for child process termination

3. **Inter-Process Communication**

   - Pipe for communication between parent and child processes
   - Non-blocking I/O for pipe reading

4. **Signal Handling**
   - SIGINT handler for clean termination
   - File I/O for high score persistence

## Future Improvements

- Add more advanced game mechanics
- Add more OS concepts like semaphores and shared memory
