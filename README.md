# NinjaFruit Game in C with SDL

## Demo Video

```bash
# Progress Update
April 18: https://www.youtube.com/watch?v=A7UfwjHtHrE
```

## Setup

```bash
# Compile the game
make

# Run the game
./ninja_fruit

# To exit the game, close the window, press Escape, or press Ctrl+C
```

### Prerequisites

You need to install SDL2, SDL2_image, and SDL2_mixer libraries:

#### On macOS (using Homebrew):

```bash
brew install sdl2 sdl2_image sdl2_mixer
```

#### On Ubuntu/Debian:

```bash
sudo apt-get install libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev
```

## Game Overview

### Game Title: NinjaFruit

### Game Summary

NinjaFruit is a Fruit Ninja style game built in C using SDL for graphics and audio. The player slices fruits that fly across the screen while avoiding bombs. The implementation showcases OS concepts including threading, forking, and pipes to manage game elements and inter-process communication. 

What sets NinjaFruit apart is its tight integration of Operating System concepts into gameplay architecture. The project serves not only as a fun game but also as a hands-on demonstration of threading, inter-process communication (IPC), forking, synchronization, signal handling, and planned deadlock detection—all implemented from the ground up in C. The game functions as both a playable experience and an educational tool for exploring low-level OS mechanics in an interactive setting.

### Core Gameplay Loop

Fruits and bombs are thrown from the bottom of the screen in random patterns. The player slices fruits with mouse movements to gain points while avoiding bombs that reduce score. Power-ups occasionally appear to provide special abilities. The challenge increases as the player progresses, with faster and more numerous objects appearing on screen.

## Gameplay Mechanics

### Controls

- **Mouse**: Drag to slice fruits and other objects
- **Keyboard**: Press Escape to exit the game

### Core Mechanics

- **Fruit Slicing**: Drag the mouse across fruits to slice them and earn points
- **Bomb Avoidance**: Avoid slicing bombs or lose points
- **Power-ups**: Special items that provide temporary abilities (managed by a child process)
- **Score System**: Points increase with fruit slices and decrease with bombs or missed fruits

### Level Progression

The game features progressive difficulty, with increasing speed and frequency of fruits and bombs as the player's score increases. This creates a natural progression curve without explicit level changes.

### Win/Loss Conditions

- **Loss**: The game ends when the player's score drops below zero
- **Win**: The goal is to achieve the highest score possible

## Technical Implementation

### OS Concepts Demonstrated

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

5. **Deadlock Detection and Handling** _(planned)_
   - Will implement resource allocation graphs to detect potential deadlocks between game subsystems
   - Recovery mechanism to safely resolve deadlocks during resource-intensive operations
   - Will improve stability during complex physics calculations and power-up interactions

### Assets

The game uses the following assets:

- **Images**: High-quality PNG images for apples, bananas, oranges, and bombs
- **Sounds**:
  - Slice sound when cutting fruits
  - Explosion sound when cutting bombs
  - Background music

## Future Improvements

- Add more varieties of fruits and obstacles
- Implement additional power-up types
- Add a combo system for slicing multiple fruits at once
- Enhance visual effects for slicing
- Add more advanced game mechanics with deadlock detection for resource management
- Implement additional OS concepts like semaphores and shared memory
- Improve mouse spicing feature so user can slice in every angle
