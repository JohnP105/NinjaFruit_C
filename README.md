# 🍊 NinjaFruit Game in C with SDL

## 🎮 Demo Video

- April 18: https://www.youtube.com/watch?v=A7UfwjHtHrE
- April 28: https://www.youtube.com/watch?v=HXFGfjR5fH0

## 🚀 Setup

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

## 📖 Game Overview

### Game Title: 🍊 NinjaFruit

### Game Summary

NinjaFruit is a Fruit Ninja style game built in C using SDL for graphics and audio. The player slices fruits that fly across the screen while avoiding bombs. The implementation showcases OS concepts including threading, forking, pipes, signal handling, and deadlock detection to manage game elements and inter-process communication.

What sets NinjaFruit apart is its tight integration of Operating System concepts into gameplay architecture. The project serves not only as a fun game but also as a hands-on demonstration of threading, inter-process communication (IPC), forking, synchronization, signal handling, and deadlock detection—all implemented from the ground up in C. The game functions as both a playable experience and an educational tool for exploring low-level OS mechanics in an interactive setting.

### Core Gameplay Loop

Fruits and bombs spawn from the top of the screen in random patterns. The player slices fruits with mouse movements to gain points while avoiding bombs that reduce health. The challenge increases as the player progresses, with faster and more numerous objects appearing on screen.

## 🎯 Gameplay Mechanics

### Controls

- **Mouse**: Drag to slice fruits and other objects
- **Keyboard**: Press Escape to exit the game

### Core Mechanics

- 🍎 **Fruit Slicing**: Drag the mouse across fruits to slice them and earn points
- 💣 **Bomb Avoidance**: Avoid slicing bombs or lose health (no score penalty)
- 🏆 **Score System**: Points increase with fruit slices

### Level Progression

The game features progressive difficulty, with increasing speed and frequency of fruits and bombs as the player's score increases. This creates a natural progression curve without explicit level changes.

### Win/Loss Conditions

- **Loss**: The game ends when the player's health drops to zero
- **Win**: The goal is to achieve the highest score possible

## 💻 Technical Implementation

### 🔧 OS CONCEPTS DEMONSTRATED

### 1. 🧵 **Threading**

```bash
pthread_t spawnerThread;
pthread_create(&spawnerThread, NULL, spawnObjects, NULL);
```

- A separate thread handles fruit spawning (pthread_create)
- Thread synchronization with mutex locks (pthread_mutex_lock/unlock)

### 2. 🔄 **Process Creation**

```bash
void processSpawner()
{
    pid_t pid = fork();

    if (pid == -1)
    {
        perror("Fork failed");
        exit(EXIT_FAILURE);
    }
```

- Fork() to create a child process for background tasks
- Wait() for child process termination

### 3. 📡 **Inter-Process Communication**

```bash
int pipefd[2];
pipe(pipefd);

pid_t pid = fork();
if (pid == 0) {
    // Child process
    close(pipefd[0]); // Close unused read end
    write(pipefd[1], "message", 7);
    close(pipefd[1]);
    exit(0);
} else {
    // Parent process
    close(pipefd[1]); // Close unused write end
    char buffer[128];
    read(pipefd[0], buffer, sizeof(buffer));
    close(pipefd[0]);
```

- Pipe for communication between parent and child processes
- Non-blocking I/O for pipe reading

### 4. 🛑 **Signal Handling**

```bash
// Signal handler for clean exit
void signalHandler(int sig)
{
    // Avoid unused parameter warning
    (void)sig;

    printf("\nGame ending. Final score: %d\n", score);
    running = 0;
    saveScore();
    cleanupGame();
    exit(0);
}
```

- SIGINT handler for clean termination
- File I/O for high score persistence

### 5. 🔒 **Deadlock Detection and Handling**

```bash
// Deadlock detection algorithm (Banker's algorithm)
int detectDeadlock() {
    pthread_mutex_lock(&deadlock_detector.deadlock_mutex);

    // Implementation of Banker's algorithm
    // Checks if processes can complete with available resources

    pthread_mutex_unlock(&deadlock_detector.deadlock_mutex);
    return deadlock_detected;
}

// Deadlock recovery function
void recoverFromDeadlock() {
    // Release resources from a deadlocked process
    // to resolve the deadlock
}
```

- Implementation of Banker's algorithm for detecting potential deadlocks
- Resource allocation tracking in a separate thread
- Recovery mechanism to safely resolve deadlocks by releasing resources
- Simulates concurrent processes competing for limited resources
- Demonstrates deadlock avoidance and recovery techniques

### Assets

The game uses the following assets:

- **Images**: High-quality PNG images for apples, bananas, oranges, and bombs
- **Sounds**:
  - Slice sound when cutting fruits
  - Explosion sound when cutting bombs
  - Background music

## 🔮 Future Improvements

- Add more varieties of fruits and obstacles
- Implement interactive power-ups that can be sliced for special abilities
- Add a combo system for slicing multiple fruits at once
- Enhance visual effects for slicing
- Improve mouse slicing feature so user can slice in every angle

### NinjaFruit Gameplay Screenshot

![NinjaFruit Gameplay](./assets/gameplay.png)
