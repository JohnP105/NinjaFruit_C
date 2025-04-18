#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/types.h>
#include <errno.h>

// Game constants
#define MAX_FRUITS 20
#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600
#define FRUIT_TYPES 4
#define BOMB_CHANCE 10 // 1 in 10 chance of spawning a bomb

// Game data structures
typedef enum
{
    APPLE,
    BANANA,
    ORANGE,
    WATERMELON,
    BOMB
} ObjectType;

typedef struct
{
    int x;           // x position
    int y;           // y position
    int velocity;    // speed
    int active;      // whether the fruit is active
    ObjectType type; // type of object
    int sliced;      // whether the fruit has been sliced
} GameObject;

// Global variables
GameObject gameObjects[MAX_FRUITS];
pthread_mutex_t game_mutex;
int score = 0;
int running = 1;
int spawn_pipe[2]; // Pipe for communicating with spawn process

// Function prototypes
void initGame();
void *spawnObjects(void *arg);
void handleSlice(int x, int y);
void updateGame();
void saveScore();
void signalHandler(int sig);
void processSpawner();

// Initialize the game
void initGame()
{
    // Initialize mutex
    pthread_mutex_init(&game_mutex, NULL);

    // Initialize random seed
    srand(time(NULL));

    // Initialize game objects
    for (int i = 0; i < MAX_FRUITS; i++)
    {
        gameObjects[i].active = 0;
    }

    // Set up signal handler for clean exit
    signal(SIGINT, signalHandler);

    // Create pipe for spawn process communication
    if (pipe(spawn_pipe) == -1)
    {
        perror("Pipe creation failed");
        exit(EXIT_FAILURE);
    }
}

// Spawn objects thread function
void *spawnObjects(void *arg)
{
    // Avoid unused parameter warning
    (void)arg;

    while (running)
    {
        // Lock mutex before modifying shared data
        pthread_mutex_lock(&game_mutex);

        // Find an inactive object slot
        for (int i = 0; i < MAX_FRUITS; i++)
        {
            if (!gameObjects[i].active)
            {
                // Random chance to spawn
                if (rand() % 30 == 0)
                {
                    gameObjects[i].active = 1;
                    gameObjects[i].x = rand() % SCREEN_WIDTH;
                    gameObjects[i].y = 0;
                    gameObjects[i].velocity = 2 + rand() % 5;
                    gameObjects[i].sliced = 0;

                    // Determine if it's a bomb or fruit
                    if (rand() % BOMB_CHANCE == 0)
                    {
                        gameObjects[i].type = BOMB;
                    }
                    else
                    {
                        gameObjects[i].type = rand() % FRUIT_TYPES;
                    }

                    break;
                }
            }
        }

        // Unlock mutex
        pthread_mutex_unlock(&game_mutex);

        // Sleep to control spawn rate
        usleep(100000); // 100ms
    }

    return NULL;
}

// Handle player slicing motion
void handleSlice(int x, int y)
{
    pthread_mutex_lock(&game_mutex);

    for (int i = 0; i < MAX_FRUITS; i++)
    {
        if (gameObjects[i].active && !gameObjects[i].sliced)
        {
            // Simple collision detection
            int dx = x - gameObjects[i].x;
            int dy = y - gameObjects[i].y;
            int distance = dx * dx + dy * dy;

            if (distance < 900)
            { // 30px radius squared
                gameObjects[i].sliced = 1;

                if (gameObjects[i].type == BOMB)
                {
                    score -= 10;
                    printf("Bomb sliced! Score: %d\n", score);
                }
                else
                {
                    score += 1;
                    printf("Fruit sliced! Score: %d\n", score);
                }
            }
        }
    }

    pthread_mutex_unlock(&game_mutex);
}

// Update game state
void updateGame()
{
    pthread_mutex_lock(&game_mutex);

    for (int i = 0; i < MAX_FRUITS; i++)
    {
        if (gameObjects[i].active)
        {
            // Update position
            gameObjects[i].y += gameObjects[i].velocity;

            // Check if out of bounds
            if (gameObjects[i].y > SCREEN_HEIGHT)
            {
                gameObjects[i].active = 0;

                // Penalty for missing a fruit (not bombs)
                if (gameObjects[i].type != BOMB && !gameObjects[i].sliced)
                {
                    score -= 1;
                    printf("Fruit missed! Score: %d\n", score);
                }
            }
        }
    }

    pthread_mutex_unlock(&game_mutex);
}

// Save high score to file
void saveScore()
{
    FILE *file = fopen("highscore.txt", "w");
    if (file != NULL)
    {
        fprintf(file, "%d", score);
        fclose(file);
        printf("Score saved: %d\n", score);
    }
    else
    {
        perror("Failed to save score");
    }
}

// Signal handler for clean exit
void signalHandler(int sig)
{
    // Avoid unused parameter warning
    (void)sig;

    printf("\nGame ending. Final score: %d\n", score);
    running = 0;
    saveScore();

    // Clean up mutex
    pthread_mutex_destroy(&game_mutex);

    // Close pipes
    close(spawn_pipe[0]);
    close(spawn_pipe[1]);

    exit(0);
}

// Process spawner function (using fork and pipe)
void processSpawner()
{
    pid_t pid = fork();

    if (pid == -1)
    {
        perror("Fork failed");
        exit(EXIT_FAILURE);
    }

    if (pid == 0)
    {
        // Child process - will handle special power-ups
        close(spawn_pipe[0]); // Close unused read end

        while (running)
        {
            // Every few seconds, create a special power-up
            sleep(5);

            if (rand() % 3 == 0)
            {
                int power_type = rand() % 2; // 0 for slow-mo, 1 for double points

                // Write power-up type to pipe
                if (write(spawn_pipe[1], &power_type, sizeof(power_type)) == -1)
                {
                    perror("Write to pipe failed");
                    break;
                }

                printf("Child process spawned power-up: %d\n", power_type);
            }
        }

        close(spawn_pipe[1]);
        exit(0);
    }
    else
    {
        // Parent process
        close(spawn_pipe[1]); // Close unused write end

        // Set non-blocking read
        fcntl(spawn_pipe[0], F_SETFL, O_NONBLOCK);
    }
}

// Simulate player input (for this text-based demo)
void simulatePlayerInput()
{
    int x = rand() % SCREEN_WIDTH;
    int y = rand() % SCREEN_HEIGHT;
    handleSlice(x, y);
}

// Check for power-ups from child process
void checkPowerUps()
{
    int power_type;
    int result = read(spawn_pipe[0], &power_type, sizeof(power_type));

    if (result > 0)
    {
        // Successfully read a power-up
        if (power_type == 0)
        {
            printf("Power-up: SLOW MOTION activated!\n");
            // Would slow down game objects in a real implementation
        }
        else
        {
            printf("Power-up: DOUBLE POINTS activated!\n");
            // Would double points for a limited time
        }
    }
    else if (result == -1 && errno != EAGAIN)
    {
        // Real error (not just non-blocking with no data)
        perror("Read from pipe failed");
    }
}

int main()
{
    printf("NinjaFruit Game Starting!\n");

    initGame();

    // Launch object spawner thread
    pthread_t spawnerThread;
    pthread_create(&spawnerThread, NULL, spawnObjects, NULL);

    // Launch power-up process
    processSpawner();

    // Main game loop
    while (running)
    {
        // Update game state
        updateGame();

        // Simulate player input
        if (rand() % 5 == 0)
        {
            simulatePlayerInput();
        }

        // Check for power-ups from child process
        checkPowerUps();

        // Render game (text-based for this demo)
        int activeCount = 0;
        pthread_mutex_lock(&game_mutex);
        for (int i = 0; i < MAX_FRUITS; i++)
        {
            if (gameObjects[i].active)
            {
                activeCount++;
            }
        }
        pthread_mutex_unlock(&game_mutex);

        printf("Active objects: %d, Score: %d\n", activeCount, score);

        usleep(200000); // 200ms
    }

    // Wait for spawner thread to finish
    pthread_join(spawnerThread, NULL);

    // Wait for child process
    wait(NULL);

    return 0;
}