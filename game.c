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
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

// Game constants
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define MAX_FRUITS 20
#define FRUIT_TYPES 4
#define BOMB_CHANCE 10 // 1 in 10 chance of spawning a bomb
#define FRUIT_SIZE 64

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
    float x;         // x position
    float y;         // y position
    float velocity;  // speed
    int active;      // whether the fruit is active
    ObjectType type; // type of object
    int sliced;      // whether the fruit has been sliced
    float rotation;  // rotation angle
    float rotSpeed;  // rotation speed
} GameObject;

// Global variables
GameObject gameObjects[MAX_FRUITS];
pthread_mutex_t game_mutex;
int score = 0;
int running = 1;
int spawn_pipe[2]; // Pipe for communicating with spawn process

// SDL related variables
SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
SDL_Texture *fruit_textures[FRUIT_TYPES];
SDL_Texture *bomb_texture = NULL;
SDL_Texture *slice_texture = NULL;
SDL_Texture *background_texture = NULL;

// Mouse tracking
int mouse_x = 0, mouse_y = 0;
int prev_mouse_x = 0, prev_mouse_y = 0;
int mouse_down = 0;

// Function prototypes
void initGame();
void *spawnObjects(void *arg);
void handleEvents();
void updateGame();
void renderGame();
void cleanupGame();
void saveScore();
void signalHandler(int sig);
void processSpawner();

// Initialize SDL and game resources
void initGame()
{
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    // Initialize SDL_image
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG))
    {
        printf("SDL_image could not initialize! SDL_image Error: %s\n", IMG_GetError());
        SDL_Quit();
        exit(EXIT_FAILURE);
    }

    // Create window
    window = SDL_CreateWindow("NinjaFruit Game", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    if (window == NULL)
    {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        IMG_Quit();
        SDL_Quit();
        exit(EXIT_FAILURE);
    }

    // Create renderer
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL)
    {
        printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        IMG_Quit();
        SDL_Quit();
        exit(EXIT_FAILURE);
    }

    // Load textures
    // Note: In a real implementation, you would need actual image files for these
    // For this example, we'll create colored rectangles as placeholders

    // Create fruit textures (colored rectangles)
    SDL_Surface *fruit_surface;

    // Apple (red)
    fruit_surface = SDL_CreateRGBSurface(0, FRUIT_SIZE, FRUIT_SIZE, 32, 0, 0, 0, 0);
    SDL_FillRect(fruit_surface, NULL, SDL_MapRGB(fruit_surface->format, 255, 0, 0));
    fruit_textures[APPLE] = SDL_CreateTextureFromSurface(renderer, fruit_surface);
    SDL_FreeSurface(fruit_surface);

    // Banana (yellow)
    fruit_surface = SDL_CreateRGBSurface(0, FRUIT_SIZE, FRUIT_SIZE, 32, 0, 0, 0, 0);
    SDL_FillRect(fruit_surface, NULL, SDL_MapRGB(fruit_surface->format, 255, 255, 0));
    fruit_textures[BANANA] = SDL_CreateTextureFromSurface(renderer, fruit_surface);
    SDL_FreeSurface(fruit_surface);

    // Orange (orange)
    fruit_surface = SDL_CreateRGBSurface(0, FRUIT_SIZE, FRUIT_SIZE, 32, 0, 0, 0, 0);
    SDL_FillRect(fruit_surface, NULL, SDL_MapRGB(fruit_surface->format, 255, 165, 0));
    fruit_textures[ORANGE] = SDL_CreateTextureFromSurface(renderer, fruit_surface);
    SDL_FreeSurface(fruit_surface);

    // Watermelon (green)
    fruit_surface = SDL_CreateRGBSurface(0, FRUIT_SIZE, FRUIT_SIZE, 32, 0, 0, 0, 0);
    SDL_FillRect(fruit_surface, NULL, SDL_MapRGB(fruit_surface->format, 0, 255, 0));
    fruit_textures[WATERMELON] = SDL_CreateTextureFromSurface(renderer, fruit_surface);
    SDL_FreeSurface(fruit_surface);

    // Bomb (black)
    fruit_surface = SDL_CreateRGBSurface(0, FRUIT_SIZE, FRUIT_SIZE, 32, 0, 0, 0, 0);
    SDL_FillRect(fruit_surface, NULL, SDL_MapRGB(fruit_surface->format, 0, 0, 0));
    bomb_texture = SDL_CreateTextureFromSurface(renderer, fruit_surface);
    SDL_FreeSurface(fruit_surface);

    // Slice effect (white)
    fruit_surface = SDL_CreateRGBSurface(0, FRUIT_SIZE / 2, FRUIT_SIZE / 2, 32, 0, 0, 0, 0);
    SDL_FillRect(fruit_surface, NULL, SDL_MapRGB(fruit_surface->format, 255, 255, 255));
    slice_texture = SDL_CreateTextureFromSurface(renderer, fruit_surface);
    SDL_FreeSurface(fruit_surface);

    // Background (dark blue)
    fruit_surface = SDL_CreateRGBSurface(0, WINDOW_WIDTH, WINDOW_HEIGHT, 32, 0, 0, 0, 0);
    SDL_FillRect(fruit_surface, NULL, SDL_MapRGB(fruit_surface->format, 0, 0, 50));
    background_texture = SDL_CreateTextureFromSurface(renderer, fruit_surface);
    SDL_FreeSurface(fruit_surface);

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
        cleanupGame();
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
                    gameObjects[i].x = rand() % (WINDOW_WIDTH - FRUIT_SIZE);
                    gameObjects[i].y = WINDOW_HEIGHT;
                    gameObjects[i].velocity = -5.0f - (rand() % 5); // Negative to go upward
                    gameObjects[i].sliced = 0;
                    gameObjects[i].rotation = 0.0f;
                    gameObjects[i].rotSpeed = 0.1f + ((float)rand() / RAND_MAX) * 0.2f;
                    if (rand() % 2)
                        gameObjects[i].rotSpeed *= -1; // Random direction

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
        usleep(50000); // 50ms
    }

    return NULL;
}

// Handle SDL events
void handleEvents()
{
    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
        if (e.type == SDL_QUIT)
        {
            running = 0;
        }
        else if (e.type == SDL_MOUSEMOTION)
        {
            prev_mouse_x = mouse_x;
            prev_mouse_y = mouse_y;
            mouse_x = e.motion.x;
            mouse_y = e.motion.y;

            // Check for slice if mouse is down
            if (mouse_down)
            {
                // Check slice along the path from prev to current
                for (int t = 0; t < 10; t++)
                {
                    float lerp = t / 10.0f;
                    int slice_x = prev_mouse_x + (mouse_x - prev_mouse_x) * lerp;
                    int slice_y = prev_mouse_y + (mouse_y - prev_mouse_y) * lerp;

                    pthread_mutex_lock(&game_mutex);
                    for (int i = 0; i < MAX_FRUITS; i++)
                    {
                        if (gameObjects[i].active && !gameObjects[i].sliced)
                        {
                            // Simple collision detection
                            float dx = slice_x - gameObjects[i].x - FRUIT_SIZE / 2;
                            float dy = slice_y - gameObjects[i].y - FRUIT_SIZE / 2;
                            float distance_squared = dx * dx + dy * dy;

                            if (distance_squared < (FRUIT_SIZE / 2) * (FRUIT_SIZE / 2))
                            {
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
            }
        }
        else if (e.type == SDL_MOUSEBUTTONDOWN)
        {
            mouse_down = 1;
        }
        else if (e.type == SDL_MOUSEBUTTONUP)
        {
            mouse_down = 0;
        }
    }
}

// Update game state
void updateGame()
{
    pthread_mutex_lock(&game_mutex);

    for (int i = 0; i < MAX_FRUITS; i++)
    {
        if (gameObjects[i].active)
        {
            // Update position - simulate a parabolic trajectory
            gameObjects[i].velocity += 0.2f; // Gravity effect
            gameObjects[i].y += gameObjects[i].velocity;
            gameObjects[i].rotation += gameObjects[i].rotSpeed;

            // Check if out of bounds
            if (gameObjects[i].y > WINDOW_HEIGHT + FRUIT_SIZE)
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

// Render the game
void renderGame()
{
    // Clear screen
    SDL_RenderClear(renderer);

    // Draw background
    SDL_RenderCopy(renderer, background_texture, NULL, NULL);

    // Draw mouse trail if mouse is down
    if (mouse_down)
    {
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderDrawLine(renderer, prev_mouse_x, prev_mouse_y, mouse_x, mouse_y);
    }

    // Draw game objects
    pthread_mutex_lock(&game_mutex);
    for (int i = 0; i < MAX_FRUITS; i++)
    {
        if (gameObjects[i].active)
        {
            SDL_Texture *texture;
            if (gameObjects[i].type == BOMB)
            {
                texture = bomb_texture;
            }
            else
            {
                texture = fruit_textures[gameObjects[i].type];
            }

            SDL_Rect dest = {
                (int)gameObjects[i].x,
                (int)gameObjects[i].y,
                FRUIT_SIZE,
                FRUIT_SIZE};

            // Draw with rotation
            SDL_RenderCopyEx(renderer, texture, NULL, &dest,
                             gameObjects[i].rotation * 57.2958f, NULL, SDL_FLIP_NONE);

            // Draw slice effect if sliced
            if (gameObjects[i].sliced)
            {
                SDL_Rect slice_dest = {
                    (int)gameObjects[i].x + FRUIT_SIZE / 4,
                    (int)gameObjects[i].y + FRUIT_SIZE / 4,
                    FRUIT_SIZE / 2,
                    FRUIT_SIZE / 2};
                SDL_RenderCopy(renderer, slice_texture, NULL, &slice_dest);
            }
        }
    }
    pthread_mutex_unlock(&game_mutex);

    // Draw score
    // In a real implementation, you would use SDL_ttf to render text
    // For this example, we'll just print the score to the console

    // Update screen
    SDL_RenderPresent(renderer);
}

// Clean up SDL resources
void cleanupGame()
{
    // Clean up textures
    for (int i = 0; i < FRUIT_TYPES; i++)
    {
        SDL_DestroyTexture(fruit_textures[i]);
    }
    SDL_DestroyTexture(bomb_texture);
    SDL_DestroyTexture(slice_texture);
    SDL_DestroyTexture(background_texture);

    // Clean up renderer and window
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    // Quit SDL subsystems
    IMG_Quit();
    SDL_Quit();

    // Clean up mutex
    pthread_mutex_destroy(&game_mutex);

    // Close pipes
    close(spawn_pipe[0]);
    close(spawn_pipe[1]);
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
    cleanupGame();
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
        // Handle SDL events
        handleEvents();

        // Update game state
        updateGame();

        // Check for power-ups from child process
        checkPowerUps();

        // Render game
        renderGame();

        // Cap to ~60 FPS
        SDL_Delay(16);
    }

    // Cleanup resources
    cleanupGame();

    // Wait for spawner thread to finish
    pthread_join(spawnerThread, NULL);

    // Wait for child process
    wait(NULL);

    return 0;
}