# NinjaFruit Game in C

A text-based implementation of a Fruit Ninja style game built in C, focusing on OS concepts including threading, forking, and pipes.

## Features

- **Threading**: Spawns fruits in a separate thread to demonstrate thread synchronization
- **Forking**: Uses a child process to manage power-ups
- **Pipes**: Communication between parent and child processes for power-up events
- **Mutex Locks**: Protects the shared game objects data structure
- **Signal Handling**: Handles SIGINT for graceful termination and score saving

## Game Mechanics

- Fruits and bombs fall from the top of the screen
- Player slices fruits by dragging the mouse (simulated in this text-based version)
- Slicing fruits increases score, slicing bombs decreases score
- Missing fruits decreases score
- Power-ups are occasionally spawned by the child process

## Building and Running

```bash
# Compile the game
make

# Run the game
./ninja_fruit

# To exit the game, press Ctrl+C (sends SIGINT)
```

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

- Add graphical interface using SDL or other graphics library
- Implement more advanced game mechanics
- Add more OS concepts like semaphores and shared memory
