# NinjaFruit Game in C with SDL

A Fruit Ninja style game built in C, using SDL for graphics. This implementation focuses on OS concepts including threading, forking, and pipes.

## Features

- **SDL Graphics**: Colorful fruit and bomb objects with rotation and slicing effects
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

You need to install SDL2 and SDL2_image libraries:

### On macOS (using Homebrew):

```bash
brew install sdl2 sdl2_image
```

### On Ubuntu/Debian:

```bash
sudo apt-get install libsdl2-dev libsdl2-image-dev
```

### On Fedora:

```bash
sudo dnf install SDL2-devel SDL2_image-devel
```

## Building and Running

```bash
# Compile the game
make

# Run the game
./ninja_fruit

# To exit the game, close the window or press Ctrl+C
```

## Controls

- Drag the mouse to slice fruits
- Avoid slicing bombs (black objects)
- Close the window or press Ctrl+C to exit

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
