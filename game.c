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

// Game constants
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define MAX_FRUITS 20
#define FRUIT_TYPES 3  // Apple, Banana, Orange
#define BOMB_CHANCE 10 // 1 in 10 chance of spawning a bomb
#define FRUIT_SIZE 64

// Game data structures
typedef enum
{
    APPLE,
    BANANA,
    ORANGE,
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
void drawFruit(ObjectType type, SDL_Rect *dest, float rotation, int sliced);

// Draw a specific fruit type
void drawFruit(ObjectType type, SDL_Rect *dest, float rotation, int sliced)
{
    // Center of the fruit
    int centerX = dest->x + dest->w / 2;
    int centerY = dest->y + dest->h / 2;
    int radius = dest->w / 2;

    // Set up rotation matrix (simplified version)
    float cosR = cos(rotation);
    float sinR = sin(rotation);

    switch (type)
    {
    case APPLE:
        // Draw red apple circle
        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);

        // Draw filled circle
        for (int y = -radius; y <= radius; y++)
        {
            for (int x = -radius; x <= radius; x++)
            {
                if (x * x + y * y <= radius * radius)
                {
                    int drawX = centerX + (int)(x * cosR - y * sinR);
                    int drawY = centerY + (int)(x * sinR + y * cosR);
                    SDL_RenderDrawPoint(renderer, drawX, drawY);
                }
            }
        }

        // Draw apple stem
        SDL_SetRenderDrawColor(renderer, 100, 50, 0, 255);
        SDL_RenderDrawLine(renderer,
                           centerX + (int)(0 * cosR - (-radius - 5) * sinR),
                           centerY + (int)(0 * sinR + (-radius - 5) * cosR),
                           centerX + (int)(0 * cosR - (-radius - 10) * sinR),
                           centerY + (int)(0 * sinR + (-radius - 10) * cosR));

        // If sliced, draw slice line
        if (sliced)
        {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderDrawLine(renderer,
                               centerX + (int)((-radius - 5) * cosR - 0 * sinR),
                               centerY + (int)((-radius - 5) * sinR + 0 * cosR),
                               centerX + (int)((radius + 5) * cosR - 0 * sinR),
                               centerY + (int)((radius + 5) * sinR + 0 * cosR));
        }
        break;

    case BANANA:
        // Draw yellow banana
        SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);

        // Draw banana arc
        for (int i = 0; i < 180; i++)
        {
            float angle = i * M_PI / 180.0;
            float x1 = radius * 1.5 * cos(angle); // Elongated
            float y1 = radius * sin(angle);

            // Add some curvature
            x1 -= radius * 0.5; // Shift to create curved shape

            // Rotate and translate
            int drawX = centerX + (int)(x1 * cosR - y1 * sinR);
            int drawY = centerY + (int)(x1 * sinR + y1 * cosR);

            // Draw a thick point
            for (int j = -2; j <= 2; j++)
            {
                for (int k = -2; k <= 2; k++)
                {
                    if (j * j + k * k <= 4)
                    {
                        SDL_RenderDrawPoint(renderer, drawX + j, drawY + k);
                    }
                }
            }
        }

        // If sliced, draw slice line
        if (sliced)
        {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderDrawLine(renderer,
                               centerX + (int)((-radius - 5) * cosR - 0 * sinR),
                               centerY + (int)((-radius - 5) * sinR + 0 * cosR),
                               centerX + (int)((radius + 5) * cosR - 0 * sinR),
                               centerY + (int)((radius + 5) * sinR + 0 * cosR));
        }
        break;

    case ORANGE:
        // Draw orange circle
        SDL_SetRenderDrawColor(renderer, 255, 165, 0, 255);

        // Draw filled circle
        for (int y = -radius; y <= radius; y++)
        {
            for (int x = -radius; x <= radius; x++)
            {
                if (x * x + y * y <= radius * radius)
                {
                    int drawX = centerX + (int)(x * cosR - y * sinR);
                    int drawY = centerY + (int)(x * sinR + y * cosR);
                    SDL_RenderDrawPoint(renderer, drawX, drawY);
                }
            }
        }

        // Add orange texture with lines
        SDL_SetRenderDrawColor(renderer, 225, 145, 0, 255);
        for (int i = 0; i < 8; i++)
        {
            float angle = i * M_PI / 4.0;
            float x1 = 0;
            float y1 = 0;
            float x2 = radius * cos(angle);
            float y2 = radius * sin(angle);

            // Rotate and translate
            int startX = centerX + (int)(x1 * cosR - y1 * sinR);
            int startY = centerY + (int)(x1 * sinR + y1 * cosR);
            int endX = centerX + (int)(x2 * cosR - y2 * sinR);
            int endY = centerY + (int)(x2 * sinR + y2 * cosR);

            SDL_RenderDrawLine(renderer, startX, startY, endX, endY);
        }

        // If sliced, draw slice line
        if (sliced)
        {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderDrawLine(renderer,
                               centerX + (int)((-radius - 5) * cosR - 0 * sinR),
                               centerY + (int)((-radius - 5) * sinR + 0 * cosR),
                               centerX + (int)((radius + 5) * cosR - 0 * sinR),
                               centerY + (int)((radius + 5) * sinR + 0 * cosR));
        }
        break;

    case BOMB:
        // Draw black bomb
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);

        // Draw filled circle
        for (int y = -radius; y <= radius; y++)
        {
            for (int x = -radius; x <= radius; x++)
            {
                if (x * x + y * y <= radius * radius)
                {
                    int drawX = centerX + (int)(x * cosR - y * sinR);
                    int drawY = centerY + (int)(x * sinR + y * cosR);
                    SDL_RenderDrawPoint(renderer, drawX, drawY);
                }
            }
        }

        // Draw bomb fuse
        SDL_SetRenderDrawColor(renderer, 150, 75, 0, 255);
        SDL_RenderDrawLine(renderer,
                           centerX + (int)(0 * cosR - (-radius) * sinR),
                           centerY + (int)(0 * sinR + (-radius) * cosR),
                           centerX + (int)((-radius / 2) * cosR - (-radius - 10) * sinR),
                           centerY + (int)((-radius / 2) * sinR + (-radius - 10) * cosR));

        // Draw bomb spark
        if ((SDL_GetTicks() / 100) % 2 == 0)
        {
            SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
            int sparkX = centerX + (int)((-radius / 2) * cosR - (-radius - 10) * sinR);
            int sparkY = centerY + (int)((-radius / 2) * sinR + (-radius - 10) * cosR);

            for (int i = 0; i < 5; i++)
            {
                float angle = (float)i * 2 * M_PI / 5.0;
                SDL_RenderDrawLine(renderer,
                                   sparkX, sparkY,
                                   sparkX + (int)(6 * cos(angle)),
                                   sparkY + (int)(6 * sin(angle)));
            }
        }

        // If sliced (game over!)
        if (sliced)
        {
            SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
            SDL_RenderDrawLine(renderer,
                               centerX + (int)((-radius - 10) * cosR - (-radius - 10) * sinR),
                               centerY + (int)((-radius - 10) * sinR + (-radius - 10) * cosR),
                               centerX + (int)((radius + 10) * cosR - (radius + 10) * sinR),
                               centerY + (int)((radius + 10) * sinR + (radius + 10) * cosR));

            SDL_RenderDrawLine(renderer,
                               centerX + (int)((-radius - 10) * cosR - (radius + 10) * sinR),
                               centerY + (int)((-radius - 10) * sinR + (radius + 10) * cosR),
                               centerX + (int)((radius + 10) * cosR - (-radius - 10) * sinR),
                               centerY + (int)((radius + 10) * sinR + (-radius - 10) * cosR));
        }
        break;

    default:
        break;
    }
}

// Initialize SDL and game resources
void initGame()
{
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    // Create window
    window = SDL_CreateWindow("NinjaFruit Game", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    if (window == NULL)
    {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_Quit();
        exit(EXIT_FAILURE);
    }

    // Create renderer
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL)
    {
        printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        exit(EXIT_FAILURE);
    }

    // Create background
    SDL_Surface *bg_surface = SDL_CreateRGBSurface(0, WINDOW_WIDTH, WINDOW_HEIGHT, 32, 0, 0, 0, 0);

    // Create a gradient background (dark blue to light blue)
    Uint32 *pixels = (Uint32 *)bg_surface->pixels;
    for (int y = 0; y < WINDOW_HEIGHT; y++)
    {
        // Calculate gradient color (dark blue at bottom, light blue at top)
        Uint8 blue = 50 + (150 * y / WINDOW_HEIGHT);
        Uint8 green = 10 + (70 * y / WINDOW_HEIGHT);
        Uint32 color = SDL_MapRGB(bg_surface->format, 10, green, blue);

        for (int x = 0; x < WINDOW_WIDTH; x++)
        {
            pixels[y * bg_surface->pitch / 4 + x] = color;
        }
    }

    // Add some stars
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    for (int i = 0; i < 100; i++)
    {
        int x = rand() % WINDOW_WIDTH;
        int y = rand() % WINDOW_HEIGHT;
        int size = 1 + rand() % 3;

        for (int sy = -size / 2; sy <= size / 2; sy++)
        {
            for (int sx = -size / 2; sx <= size / 2; sx++)
            {
                if (x + sx >= 0 && x + sx < WINDOW_WIDTH && y + sy >= 0 && y + sy < WINDOW_HEIGHT)
                {
                    pixels[(y + sy) * bg_surface->pitch / 4 + (x + sx)] = SDL_MapRGB(bg_surface->format, 255, 255, 255);
                }
            }
        }
    }

    background_texture = SDL_CreateTextureFromSurface(renderer, bg_surface);
    SDL_FreeSurface(bg_surface);

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
                    gameObjects[i].velocity = -10.0f - (rand() % 8); // Negative to go upward
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

    // Draw score
    char score_text[32];
    sprintf(score_text, "Score: %d", score);

    // Draw score as plain text using primitives
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_Rect score_bg = {10, 10, 120, 30};
    SDL_RenderFillRect(renderer, &score_bg);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_Rect score_outline = {10, 10, 120, 30};
    SDL_RenderDrawRect(renderer, &score_outline);

    // We don't have SDL_ttf, so we'll show the score in console

    // Draw mouse trail if mouse is down
    if (mouse_down)
    {
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

        // Draw a thicker line for better visibility
        for (int i = -1; i <= 1; i++)
        {
            for (int j = -1; j <= 1; j++)
            {
                SDL_RenderDrawLine(renderer,
                                   prev_mouse_x + i, prev_mouse_y + j,
                                   mouse_x + i, mouse_y + j);
            }
        }
    }

    // Draw game objects
    pthread_mutex_lock(&game_mutex);
    for (int i = 0; i < MAX_FRUITS; i++)
    {
        if (gameObjects[i].active)
        {
            SDL_Rect dest = {
                (int)gameObjects[i].x,
                (int)gameObjects[i].y,
                FRUIT_SIZE,
                FRUIT_SIZE};

            // Draw the fruit using our custom function
            drawFruit(gameObjects[i].type, &dest, gameObjects[i].rotation, gameObjects[i].sliced);
        }
    }
    pthread_mutex_unlock(&game_mutex);

    // Update screen
    SDL_RenderPresent(renderer);
}

// Clean up SDL resources
void cleanupGame()
{
    // Clean up texture
    SDL_DestroyTexture(background_texture);

    // Clean up renderer and window
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    // Quit SDL subsystems
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