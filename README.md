# NinjaFruit Game in C with SDL

A Fruit Ninja style game built in C, using SDL for graphics. This implementation focuses on OS concepts including threading, forking, and pipes.

## Features

- **SDL Graphics**: Colorful custom-drawn fruits (apple, banana, orange) and bombs with rotation and slicing effects
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

You need to install SDL2 library:

### On macOS (using Homebrew):

```bash
brew install sdl2
```

### On Ubuntu/Debian:

```bash
sudo apt-get install libsdl2-dev
```

### On Fedora:

```bash
sudo dnf install SDL2-devel
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

## Visuals

- **Apple**: Red circle with a stem
- **Banana**: Yellow curved shape
- **Orange**: Orange circle with segments
- **Bomb**: Black circle with a fuse and animated spark

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
