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
#include <SDL2/SDL_mixer.h>
#include <math.h>
#include <stdbool.h>

// Game constants
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define MAX_FRUITS 20
#define FRUIT_TYPES 3  // Apple, Banana, Orange
#define BOMB_CHANCE 10 // 1 in 10 chance of spawning a bomb
#define FRUIT_SIZE 64

// Slicing animation constants
#define SLICE_PIECES 2
#define SLICE_DURATION 30 // frames

// Game data structures
typedef enum
{
    APPLE,
    BANANA,
    ORANGE,
    BOMB
} ObjectType;

typedef struct SlicePiece
{
    float x;
    float y;
    float vx;
    float vy;
    float rotation;
    float rotSpeed;
    int timeLeft;
} SlicePiece;

typedef struct
{
    float x;                         // x position
    float y;                         // y position
    float vx;                        // x velocity component
    float vy;                        // y velocity component
    int active;                      // whether the fruit is active
    ObjectType type;                 // type of object
    int sliced;                      // whether the fruit has been sliced
    float rotation;                  // rotation angle
    float rotSpeed;                  // rotation speed
    SlicePiece pieces[SLICE_PIECES]; // Pieces when sliced
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
SDL_Texture *background_texture = NULL;

// Sound effects
Mix_Chunk *sliceSound = NULL;
Mix_Chunk *bombSound = NULL;
Mix_Music *backgroundMusic = NULL;

// Mouse tracking
int mouse_x = 0, mouse_y = 0;
int prev_mouse_x = 0, prev_mouse_y = 0;
int mouse_down = 0;

// Function prototypes
int initGame(void);
void *spawnObjects(void *arg);
void handleEvents();
void updateGame();
void renderGame();
void cleanupGame();
void saveScore();
void signalHandler(int sig);
void processSpawner();
void drawFruit(ObjectType type, float x, float y, float rotation, int sliced);
void filledCircleRGBA(SDL_Renderer *renderer, int x, int y, int radius, Uint8 r, Uint8 g, Uint8 b, Uint8 a);

// Draw fruit function - renders different types of fruits/bombs
void drawFruit(ObjectType type, float x, float y, float rotation, int sliced)
{
    const int halfSize = FRUIT_SIZE / 2;

    // Set up transformation (translate to center and rotate)
    SDL_Point center = {halfSize, halfSize};
    SDL_Rect dest = {x - halfSize, y - halfSize, FRUIT_SIZE, FRUIT_SIZE};

    // Different colors and shapes for different fruits
    switch (type)
    {
    case APPLE:
        if (!sliced)
        {
            // Red apple
            SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
            filledCircleRGBA(renderer, x, y, halfSize - 5, 255, 0, 0, 255);

            // Stem
            SDL_SetRenderDrawColor(renderer, 139, 69, 19, 255);
            SDL_Rect stem = {x - 3, y - halfSize + 5, 6, 10};
            SDL_RenderFillRect(renderer, &stem);

            // Leaf
            SDL_SetRenderDrawColor(renderer, 0, 128, 0, 255);
            SDL_Point leaf[3] = {
                {x + 8, y - halfSize + 8},
                {x + 18, y - halfSize + 2},
                {x + 6, y - halfSize + 2}};
            SDL_RenderDrawLines(renderer, leaf, 3);
            SDL_RenderFillRect(renderer, &stem);
        }
        else
        {
            // Sliced apple - two halves
            SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
            filledCircleRGBA(renderer, x - 15, y, halfSize - 10, 255, 0, 0, 255);
            filledCircleRGBA(renderer, x + 15, y, halfSize - 10, 255, 0, 0, 255);

            // White inside
            filledCircleRGBA(renderer, x - 15, y, halfSize - 15, 255, 240, 240, 255);
            filledCircleRGBA(renderer, x + 15, y, halfSize - 15, 255, 240, 240, 255);
        }
        break;

    case BANANA:
        if (!sliced)
        {
            // Yellow banana (curved rectangle)
            SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);

            // Draw a curved banana shape
            for (int i = -20; i <= 20; i++)
            {
                float angle = (float)i / 20.0f * 3.14f;
                float cx = x + cos(angle + rotation) * halfSize * 0.8f;
                float cy = y + sin(angle + rotation) * halfSize * 0.3f;
                filledCircleRGBA(renderer, cx, cy, 8, 255, 255, 0, 255);
            }
        }
        else
        {
            // Sliced banana
            SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);

            for (int i = -10; i <= 0; i++)
            {
                float angle = (float)i / 10.0f * 3.14f;
                float cx = x - 15 + cos(angle + rotation) * halfSize * 0.7f;
                float cy = y + sin(angle + rotation) * halfSize * 0.3f;
                filledCircleRGBA(renderer, cx, cy, 6, 255, 255, 0, 255);
            }

            for (int i = 0; i <= 10; i++)
            {
                float angle = (float)i / 10.0f * 3.14f;
                float cx = x + 15 + cos(angle + rotation) * halfSize * 0.7f;
                float cy = y + sin(angle + rotation) * halfSize * 0.3f;
                filledCircleRGBA(renderer, cx, cy, 6, 255, 255, 0, 255);
            }
        }
        break;

    case ORANGE:
        if (!sliced)
        {
            // Orange circle
            filledCircleRGBA(renderer, x, y, halfSize - 5, 255, 165, 0, 255);

            // Texture dots
            SDL_SetRenderDrawColor(renderer, 200, 120, 0, 255);
            for (int i = 0; i < 10; i++)
            {
                float angle = 2.0f * 3.14f * i / 10.0f + rotation;
                float cx = x + cos(angle) * (halfSize - 15);
                float cy = y + sin(angle) * (halfSize - 15);
                filledCircleRGBA(renderer, cx, cy, 3, 200, 120, 0, 255);
            }
        }
        else
        {
            // Sliced orange - two halves with visible inside
            filledCircleRGBA(renderer, x - 15, y, halfSize - 10, 255, 165, 0, 255);
            filledCircleRGBA(renderer, x + 15, y, halfSize - 10, 255, 165, 0, 255);

            // Inside pulp
            filledCircleRGBA(renderer, x - 15, y, halfSize - 15, 255, 200, 150, 255);
            filledCircleRGBA(renderer, x + 15, y, halfSize - 15, 255, 200, 150, 255);

            // Segment lines
            SDL_SetRenderDrawColor(renderer, 255, 140, 0, 255);
            for (int i = 0; i < 6; i++)
            {
                float angle = 3.14f * i / 6.0f;
                SDL_RenderDrawLine(renderer,
                                   x - 15, y,
                                   x - 15 + cos(angle) * (halfSize - 15),
                                   y + sin(angle) * (halfSize - 15));

                SDL_RenderDrawLine(renderer,
                                   x + 15, y,
                                   x + 15 + cos(angle) * (halfSize - 15),
                                   y + sin(angle) * (halfSize - 15));
            }
        }
        break;

    case BOMB:
        if (!sliced)
        {
            // Black bomb
            filledCircleRGBA(renderer, x, y, halfSize - 5, 10, 10, 10, 255);

            // Fuse
            SDL_SetRenderDrawColor(renderer, 160, 120, 80, 255);
            SDL_Rect fuse = {x - 2, y - halfSize - 5, 4, 15};
            SDL_RenderFillRect(renderer, &fuse);

            // Spark
            SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
            SDL_Rect spark = {x - 4, y - halfSize - 10, 8, 8};
            SDL_RenderFillRect(renderer, &spark);
        }
        else
        {
            // Explosion effect
            for (int i = 0; i < 20; i++)
            {
                float angle = 2.0f * 3.14f * i / 20.0f;
                float distance = (halfSize - 5) * (1.0f + ((float)rand() / RAND_MAX) * 0.5f);
                float cx = x + cos(angle) * distance;
                float cy = y + sin(angle) * distance;

                Uint8 r = 200 + rand() % 56;
                Uint8 g = 100 + rand() % 100;
                Uint8 b = rand() % 50;

                filledCircleRGBA(renderer, cx, cy, 5 + rand() % 8, r, g, b, 255);
            }
        }
        break;
    }
}

// Helper function for drawing filled circles since SDL doesn't provide one
void filledCircleRGBA(SDL_Renderer *renderer, int x, int y, int radius, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    SDL_SetRenderDrawColor(renderer, r, g, b, a);

    for (int w = 0; w < radius * 2; w++)
    {
        for (int h = 0; h < radius * 2; h++)
        {
            int dx = radius - w;
            int dy = radius - h;
            if ((dx * dx + dy * dy) <= (radius * radius))
            {
                SDL_RenderDrawPoint(renderer, x + dx - radius, y + dy - radius);
            }
        }
    }
}

// Function to initialize the game
int initGame(void)
{
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
        printf("SDL could not initialize! SDL Error: %s\n", SDL_GetError());
        return 0;
    }

    // Create window
    window = SDL_CreateWindow("Ninja Fruit",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              WINDOW_WIDTH, WINDOW_HEIGHT,
                              SDL_WINDOW_SHOWN);
    if (window == NULL)
    {
        printf("Window could not be created! SDL Error: %s\n", SDL_GetError());
        return 0;
    }

    // Create renderer
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL)
    {
        printf("Renderer could not be created! SDL Error: %s\n", SDL_GetError());
        return 0;
    }

    // Initialize SDL_mixer
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0)
    {
        printf("SDL_mixer could not initialize! SDL_mixer Error: %s\n", Mix_GetError());
        // Continue without sound
    }

    // Load sound effects
    sliceSound = Mix_LoadWAV("sounds/slice.wav");
    bombSound = Mix_LoadWAV("sounds/explosion.wav");
    backgroundMusic = Mix_LoadMUS("sounds/background.mp3");

    if (sliceSound == NULL || bombSound == NULL || backgroundMusic == NULL)
    {
        printf("Warning: Could not load sounds! SDL_mixer Error: %s\n", Mix_GetError());
        // Continue without sound
    }

    // Create a solid color background if no background image is available
    background_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                           SDL_TEXTUREACCESS_TARGET, WINDOW_WIDTH, WINDOW_HEIGHT);

    if (background_texture == NULL)
    {
        printf("Background texture could not be created! SDL Error: %s\n", SDL_GetError());
        // Create a simple colored background
        background_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                               SDL_TEXTUREACCESS_TARGET, WINDOW_WIDTH, WINDOW_HEIGHT);

        if (background_texture == NULL)
        {
            printf("Could not create fallback background texture! SDL Error: %s\n", SDL_GetError());
            // Continue without background
        }
        else
        {
            // Set render target to the background texture
            SDL_SetRenderTarget(renderer, background_texture);

            // Fill with dark blue
            SDL_SetRenderDrawColor(renderer, 10, 10, 50, 255);
            SDL_RenderClear(renderer);

            // Draw some stars
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            for (int i = 0; i < 100; i++)
            {
                int x = rand() % WINDOW_WIDTH;
                int y = rand() % WINDOW_HEIGHT;
                SDL_Rect star = {x, y, 2, 2};
                SDL_RenderFillRect(renderer, &star);
            }

            // Reset render target
            SDL_SetRenderTarget(renderer, NULL);
        }
    }

    // Initialize mutex
    pthread_mutex_init(&game_mutex, NULL);

    // Initialize game objects
    for (int i = 0; i < MAX_FRUITS; i++)
    {
        gameObjects[i].active = 0;
    }

    // Set up spawn pipe
    if (pipe(spawn_pipe) != 0)
    {
        printf("Failed to create pipe\n");
        return 0;
    }

    // Play background music
    Mix_PlayMusic(backgroundMusic, -1);

    return 1;
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
                    gameObjects[i].y = 0; // Drop from the top of the screen

                    // Random horizontal velocity component
                    gameObjects[i].vx = -3.0f + (rand() % 60) / 10.0f;
                    gameObjects[i].vy = 2.0f + (rand() % 30) / 10.0f; // Positive to go downward

                    gameObjects[i].sliced = 0;
                    gameObjects[i].rotation = 0.0f;
                    gameObjects[i].rotSpeed = 0.05f + ((float)rand() / RAND_MAX) * 0.1f;
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

                    // Initialize slice pieces (will be used when sliced)
                    for (int j = 0; j < SLICE_PIECES; j++)
                    {
                        gameObjects[i].pieces[j].timeLeft = 0;
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

                                // Initialize slice pieces
                                float sliceAngle = atan2(mouse_y - prev_mouse_y, mouse_x - prev_mouse_x);

                                // Create two pieces moving in different directions
                                for (int j = 0; j < SLICE_PIECES; j++)
                                {
                                    gameObjects[i].pieces[j].x = gameObjects[i].x + FRUIT_SIZE / 2;
                                    gameObjects[i].pieces[j].y = gameObjects[i].y + FRUIT_SIZE / 2;

                                    // Different velocities for each piece
                                    float pieceAngle = sliceAngle + (j == 0 ? M_PI / 2 : -M_PI / 2);
                                    float speed = 2.0f + (rand() % 20) / 10.0f;

                                    gameObjects[i].pieces[j].vx = cos(pieceAngle) * speed;
                                    gameObjects[i].pieces[j].vy = sin(pieceAngle) * speed + gameObjects[i].vy / 2;
                                    gameObjects[i].pieces[j].rotation = gameObjects[i].rotation;
                                    gameObjects[i].pieces[j].rotSpeed = gameObjects[i].rotSpeed * 2.0f * (j == 0 ? 1 : -1);
                                    gameObjects[i].pieces[j].timeLeft = SLICE_DURATION;
                                }

                                if (gameObjects[i].type == BOMB)
                                {
                                    // Play bomb sound
                                    Mix_PlayChannel(-1, bombSound, 0);
                                    score -= 10;
                                    printf("Bomb sliced! Score: %d\n", score);
                                }
                                else
                                {
                                    // Play slice sound
                                    Mix_PlayChannel(-1, sliceSound, 0);
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
        else if (e.type == SDL_KEYDOWN)
        {
            if (e.key.keysym.sym == SDLK_ESCAPE)
            {
                running = 0;
            }
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
            // Update main fruit position
            gameObjects[i].vy += 0.2f; // Gravity effect
            gameObjects[i].x += gameObjects[i].vx;
            gameObjects[i].y += gameObjects[i].vy;
            gameObjects[i].rotation += gameObjects[i].rotSpeed;

            // Update slice pieces if sliced
            if (gameObjects[i].sliced)
            {
                for (int j = 0; j < SLICE_PIECES; j++)
                {
                    if (gameObjects[i].pieces[j].timeLeft > 0)
                    {
                        gameObjects[i].pieces[j].vy += 0.3f; // Heavier gravity for pieces
                        gameObjects[i].pieces[j].x += gameObjects[i].pieces[j].vx;
                        gameObjects[i].pieces[j].y += gameObjects[i].pieces[j].vy;
                        gameObjects[i].pieces[j].rotation += gameObjects[i].pieces[j].rotSpeed;
                        gameObjects[i].pieces[j].timeLeft--;
                    }
                }
            }

            // Check if out of bounds
            if (gameObjects[i].y > WINDOW_HEIGHT + FRUIT_SIZE ||
                gameObjects[i].x < -FRUIT_SIZE ||
                gameObjects[i].x > WINDOW_WIDTH + FRUIT_SIZE)
            {
                // Check if all animation is complete
                bool animationDone = true;
                if (gameObjects[i].sliced)
                {
                    for (int j = 0; j < SLICE_PIECES; j++)
                    {
                        if (gameObjects[i].pieces[j].timeLeft > 0)
                        {
                            animationDone = false;
                            break;
                        }
                    }
                }

                if (animationDone)
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
    }

    pthread_mutex_unlock(&game_mutex);
}

// Render the game
void renderGame()
{
    // Lock mutex before rendering
    pthread_mutex_lock(&game_mutex);

    // Clear screen
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    // Draw background
    SDL_RenderCopy(renderer, background_texture, NULL, NULL);

    // Draw score
    char score_text[50];
    sprintf(score_text, "Score: %d", score);
    SDL_Color textColor = {255, 255, 255, 255};

    // Instead of using SDL_ttf, just draw a simple text background
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150);
    SDL_Rect scoreRect = {10, 10, 100, 30};
    SDL_RenderFillRect(renderer, &scoreRect);

    // Draw each game object
    for (int i = 0; i < MAX_FRUITS; i++)
    {
        if (gameObjects[i].active)
        {
            if (!gameObjects[i].sliced)
            {
                // Draw unsliced fruit/bomb
                drawFruit(gameObjects[i].type, gameObjects[i].x, gameObjects[i].y,
                          gameObjects[i].rotation, 0);
            }
            else
            {
                // Draw sliced pieces if they still have time left
                for (int j = 0; j < SLICE_PIECES; j++)
                {
                    if (gameObjects[i].pieces[j].timeLeft > 0)
                    {
                        drawFruit(gameObjects[i].type,
                                  gameObjects[i].pieces[j].x,
                                  gameObjects[i].pieces[j].y,
                                  gameObjects[i].pieces[j].rotation, 1);
                    }
                }
            }
        }
    }

    // Draw slicing effect when mouse is down
    if (mouse_down && (prev_mouse_x != mouse_x || prev_mouse_y != mouse_y))
    {
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 150);
        SDL_RenderDrawLine(renderer, prev_mouse_x, prev_mouse_y, mouse_x, mouse_y);
    }

    // Present rendered frame
    SDL_RenderPresent(renderer);

    // Unlock mutex after rendering
    pthread_mutex_unlock(&game_mutex);
}

// Function to clean up resources
void cleanupGame(void)
{
    // Free sounds
    if (sliceSound != NULL)
    {
        Mix_FreeChunk(sliceSound);
        sliceSound = NULL;
    }

    if (bombSound != NULL)
    {
        Mix_FreeChunk(bombSound);
        bombSound = NULL;
    }

    if (backgroundMusic != NULL)
    {
        Mix_FreeMusic(backgroundMusic);
        backgroundMusic = NULL;
    }

    // Free background texture
    if (background_texture != NULL)
    {
        SDL_DestroyTexture(background_texture);
        background_texture = NULL;
    }

    // Destroy renderer and window
    if (renderer != NULL)
    {
        SDL_DestroyRenderer(renderer);
        renderer = NULL;
    }

    if (window != NULL)
    {
        SDL_DestroyWindow(window);
        window = NULL;
    }

    // Close audio
    Mix_CloseAudio();

    // Quit SDL subsystems
    SDL_Quit();

    // Destroy mutex
    pthread_mutex_destroy(&game_mutex);

    // Close pipe
    close(spawn_pipe[0]);
    close(spawn_pipe[1]);

    printf("Game cleaned up successfully\n");
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