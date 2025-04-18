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

    // Different colors and shapes for different fruits
    switch (type)
    {
    case APPLE:
        if (!sliced)
        {
            // Red apple with gradient
            filledCircleRGBA(renderer, x, y, halfSize - 5, 220, 0, 0, 255);
            filledCircleRGBA(renderer, x, y, halfSize - 8, 255, 30, 30, 255);
            // Highlight
            filledCircleRGBA(renderer, x - halfSize / 3, y - halfSize / 3, halfSize / 4, 255, 100, 100, 200);

            // Stem
            SDL_SetRenderDrawColor(renderer, 139, 69, 19, 255);
            SDL_Rect stem = {x - 3, y - halfSize + 5, 6, 10};
            SDL_RenderFillRect(renderer, &stem);

            // Leaf
            SDL_SetRenderDrawColor(renderer, 0, 150, 0, 255);
            SDL_Point leaf[4] = {
                {x + 6, y - halfSize + 8},
                {x + 18, y - halfSize + 2},
                {x + 15, y - halfSize + 6},
                {x + 6, y - halfSize + 12}};
            int numPoints = 4;
            SDL_RenderDrawLines(renderer, leaf, numPoints);

            // Fill leaf with gradient
            for (int i = 0; i < 5; i++)
            {
                SDL_SetRenderDrawColor(renderer, 0, 150 - i * 10, 0, 255);
                SDL_Point leafFill[] = {
                    {x + 6, y - halfSize + 8 + i},
                    {x + 15 - i, y - halfSize + 5},
                    {x + 10, y - halfSize + 10}};
                SDL_RenderDrawLines(renderer, leafFill, 3);
            }
        }
        else
        {
            // Sliced apple - two halves with more detail
            // Left half
            filledCircleRGBA(renderer, x - 15, y, halfSize - 10, 220, 0, 0, 255);
            filledCircleRGBA(renderer, x - 15, y, halfSize - 13, 240, 20, 20, 255);

            // Right half
            filledCircleRGBA(renderer, x + 15, y, halfSize - 10, 220, 0, 0, 255);
            filledCircleRGBA(renderer, x + 15, y, halfSize - 13, 240, 20, 20, 255);

            // White inside with seeds and flesh details
            filledCircleRGBA(renderer, x - 15, y, halfSize - 15, 255, 240, 240, 255);
            filledCircleRGBA(renderer, x + 15, y, halfSize - 15, 255, 240, 240, 255);

            // Seeds
            SDL_SetRenderDrawColor(renderer, 80, 40, 0, 255);
            for (int i = 0; i < 5; i++)
            {
                float angle = M_PI * i / 5.0;
                SDL_Rect seed1 = {
                    x - 15 + cos(angle) * (halfSize - 25) - 1,
                    y + sin(angle) * (halfSize - 25) - 2,
                    3, 4};
                SDL_Rect seed2 = {
                    x + 15 + cos(angle) * (halfSize - 25) - 1,
                    y + sin(angle) * (halfSize - 25) - 2,
                    3, 4};
                SDL_RenderFillRect(renderer, &seed1);
                SDL_RenderFillRect(renderer, &seed2);
            }

            // Flesh details
            SDL_SetRenderDrawColor(renderer, 230, 210, 210, 255);
            for (int i = 0; i < 8; i++)
            {
                float angle = 2 * M_PI * i / 8.0;
                SDL_RenderDrawLine(renderer,
                                   x - 15, y,
                                   x - 15 + cos(angle) * (halfSize - 17),
                                   y + sin(angle) * (halfSize - 17));
                SDL_RenderDrawLine(renderer,
                                   x + 15, y,
                                   x + 15 + cos(angle) * (halfSize - 17),
                                   y + sin(angle) * (halfSize - 17));
            }
        }
        break;

    case BANANA:
        if (!sliced)
        {
            // Yellow banana with gradient and curvature
            SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);

            // Draw a curved banana shape with gradient
            for (int i = -20; i <= 20; i++)
            {
                float angle = (float)i / 20.0f * 3.14f;
                float cx = x + cos(angle + rotation) * halfSize * 0.8f;
                float cy = y + sin(angle + rotation) * halfSize * 0.3f;

                // Gradient from yellow to slightly darker yellow
                int shade = 255 - abs(i) * 3;
                filledCircleRGBA(renderer, cx, cy, 8, shade, shade, 0, 255);
            }

            // Add shadows and highlights
            for (int i = -18; i <= -5; i++)
            {
                float angle = (float)i / 20.0f * 3.14f;
                float cx = x + cos(angle + rotation) * halfSize * 0.75f;
                float cy = y + sin(angle + rotation) * halfSize * 0.25f;
                filledCircleRGBA(renderer, cx, cy, 3, 255, 255, 150, 150);
            }

            // Banana ends (darker)
            for (int i = -20; i <= -18; i++)
            {
                float angle = (float)i / 20.0f * 3.14f;
                float cx = x + cos(angle + rotation) * halfSize * 0.8f;
                float cy = y + sin(angle + rotation) * halfSize * 0.3f;
                filledCircleRGBA(renderer, cx, cy, 6, 200, 180, 0, 255);
            }

            for (int i = 18; i <= 20; i++)
            {
                float angle = (float)i / 20.0f * 3.14f;
                float cx = x + cos(angle + rotation) * halfSize * 0.8f;
                float cy = y + sin(angle + rotation) * halfSize * 0.3f;
                filledCircleRGBA(renderer, cx, cy, 6, 200, 180, 0, 255);
            }
        }
        else
        {
            // Sliced banana with more detailed inside
            SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);

            // Left half
            for (int i = -10; i <= 0; i++)
            {
                float angle = (float)i / 10.0f * 3.14f;
                float cx = x - 15 + cos(angle + rotation) * halfSize * 0.7f;
                float cy = y + sin(angle + rotation) * halfSize * 0.3f;
                filledCircleRGBA(renderer, cx, cy, 6, 255, 255, 30, 255);
            }

            // Right half
            for (int i = 0; i <= 10; i++)
            {
                float angle = (float)i / 10.0f * 3.14f;
                float cx = x + 15 + cos(angle + rotation) * halfSize * 0.7f;
                float cy = y + sin(angle + rotation) * halfSize * 0.3f;
                filledCircleRGBA(renderer, cx, cy, 6, 255, 255, 30, 255);
            }

            // Inside (creamy white)
            for (int i = -8; i <= 0; i++)
            {
                float angle = (float)i / 8.0f * 3.14f;
                float cx = x - 15 + cos(angle + rotation) * halfSize * 0.5f;
                float cy = y + sin(angle + rotation) * halfSize * 0.2f;
                filledCircleRGBA(renderer, cx, cy, 4, 255, 250, 220, 255);
            }

            for (int i = 0; i <= 8; i++)
            {
                float angle = (float)i / 8.0f * 3.14f;
                float cx = x + 15 + cos(angle + rotation) * halfSize * 0.5f;
                float cy = y + sin(angle + rotation) * halfSize * 0.2f;
                filledCircleRGBA(renderer, cx, cy, 4, 255, 250, 220, 255);
            }

            // Seeds
            SDL_SetRenderDrawColor(renderer, 30, 30, 0, 255);
            for (int i = -2; i <= 2; i++)
            {
                SDL_Rect seed1 = {x - 15 + i * 5, y, 2, 2};
                SDL_Rect seed2 = {x + 15 + i * 5, y, 2, 2};
                SDL_RenderFillRect(renderer, &seed1);
                SDL_RenderFillRect(renderer, &seed2);
            }
        }
        break;

    case ORANGE:
        if (!sliced)
        {
            // Orange with texture and gradient
            filledCircleRGBA(renderer, x, y, halfSize - 5, 255, 140, 0, 255);
            filledCircleRGBA(renderer, x, y, halfSize - 8, 255, 165, 0, 255);
            // Highlight
            filledCircleRGBA(renderer, x - halfSize / 3, y - halfSize / 3, halfSize / 4, 255, 200, 100, 200);

            // Texture dots
            SDL_SetRenderDrawColor(renderer, 200, 120, 0, 255);
            for (int i = 0; i < 20; i++)
            {
                float angle = 2.0f * 3.14f * i / 20.0f + rotation;
                float radius = halfSize - 10 - (rand() % 8);
                float cx = x + cos(angle) * radius;
                float cy = y + sin(angle) * radius;
                filledCircleRGBA(renderer, cx, cy, 2, 220, 140, 0, 200);
            }

            // Stem/leaf detail at top
            SDL_SetRenderDrawColor(renderer, 50, 100, 0, 255);
            SDL_Rect stem = {x - 4, y - halfSize + 2, 8, 6};
            SDL_RenderFillRect(renderer, &stem);

            // Small leaf
            SDL_SetRenderDrawColor(renderer, 0, 130, 0, 255);
            SDL_Point leaf[3] = {
                {x, y - halfSize + 5},
                {x + 10, y - halfSize},
                {x + 5, y - halfSize + 8}};
            SDL_RenderDrawLines(renderer, leaf, 3);
        }
        else
        {
            // Sliced orange with detailed segments
            // Outer rind
            filledCircleRGBA(renderer, x - 15, y, halfSize - 10, 255, 140, 0, 255);
            filledCircleRGBA(renderer, x + 15, y, halfSize - 10, 255, 140, 0, 255);

            // White pith layer
            filledCircleRGBA(renderer, x - 15, y, halfSize - 12, 255, 220, 180, 255);
            filledCircleRGBA(renderer, x + 15, y, halfSize - 12, 255, 220, 180, 255);

            // Inside pulp
            filledCircleRGBA(renderer, x - 15, y, halfSize - 15, 255, 180, 100, 255);
            filledCircleRGBA(renderer, x + 15, y, halfSize - 15, 255, 180, 100, 255);

            // Segment lines with thickness
            SDL_SetRenderDrawColor(renderer, 255, 200, 150, 255);
            for (int i = 0; i < 8; i++)
            {
                float angle = 2.0f * M_PI * i / 8.0f;
                for (int w = -1; w <= 1; w++)
                {
                    SDL_RenderDrawLine(renderer,
                                       x - 15, y,
                                       x - 15 + cos(angle + w * 0.05) * (halfSize - 15),
                                       y + sin(angle + w * 0.05) * (halfSize - 15));

                    SDL_RenderDrawLine(renderer,
                                       x + 15, y,
                                       x + 15 + cos(angle + w * 0.05) * (halfSize - 15),
                                       y + sin(angle + w * 0.05) * (halfSize - 15));
                }
            }

            // Seeds at center
            SDL_SetRenderDrawColor(renderer, 255, 240, 200, 255);
            filledCircleRGBA(renderer, x - 15, y, 5, 255, 240, 200, 255);
            filledCircleRGBA(renderer, x + 15, y, 5, 255, 240, 200, 255);

            // Individual seeds
            SDL_SetRenderDrawColor(renderer, 200, 160, 50, 255);
            for (int i = 0; i < 5; i++)
            {
                float angle = 2.0f * M_PI * i / 5.0f;
                SDL_Rect seed1 = {
                    x - 15 + cos(angle) * 3 - 1,
                    y + sin(angle) * 3 - 1,
                    2, 3};
                SDL_Rect seed2 = {
                    x + 15 + cos(angle) * 3 - 1,
                    y + sin(angle) * 3 - 1,
                    2, 3};
                SDL_RenderFillRect(renderer, &seed1);
                SDL_RenderFillRect(renderer, &seed2);
            }
        }
        break;

    case BOMB:
        if (!sliced)
        {
            // Black bomb with metallic sheen
            filledCircleRGBA(renderer, x, y, halfSize - 5, 20, 20, 20, 255);

            // Metallic highlight
            filledCircleRGBA(renderer, x - halfSize / 4, y - halfSize / 4, halfSize / 3, 40, 40, 40, 200);
            filledCircleRGBA(renderer, x - halfSize / 3, y - halfSize / 3, halfSize / 6, 70, 70, 70, 200);

            // Fuse
            SDL_SetRenderDrawColor(renderer, 160, 120, 80, 255);
            // Wavy fuse
            for (int i = 0; i < 15; i++)
            {
                float wave = sin(i * 0.5) * 3;
                SDL_Rect fuseBit = {
                    x - 2 + wave,
                    y - halfSize - 5 + i,
                    4,
                    2};
                SDL_RenderFillRect(renderer, &fuseBit);
            }

            // Spark (animated)
            static float sparkPhase = 0.0f;
            sparkPhase += 0.1f;

            SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
            filledCircleRGBA(renderer,
                             x + sin(sparkPhase) * 3,
                             y - halfSize - 10 + cos(sparkPhase) * 2,
                             4 + sin(sparkPhase + 1.0f) * 2,
                             255, 200 + sin(sparkPhase) * 55, 0, 255);

            // Inner glow
            filledCircleRGBA(renderer,
                             x + sin(sparkPhase) * 2,
                             y - halfSize - 10 + cos(sparkPhase) * 1,
                             2,
                             255, 255, 200, 255);
        }
        else
        {
            // Explosion effect with more detail and animation
            static float explosionPhase = 0.0f;
            explosionPhase += 0.05f;

            // Central flash
            filledCircleRGBA(renderer, x, y, halfSize, 255, 255, 200, 150);

            // Fiery explosion particles
            for (int i = 0; i < 30; i++)
            {
                float angle = 2.0f * M_PI * i / 30.0f + explosionPhase;
                float speedVar = 0.6f + 0.4f * sin(i + explosionPhase);
                float distance = (halfSize - 5) * (1.0f + ((float)rand() / RAND_MAX) * 0.8f) * speedVar;
                float cx = x + cos(angle) * distance;
                float cy = y + sin(angle) * distance;

                // Fire colors
                Uint8 r = 220 + rand() % 36;
                Uint8 g = 100 + (i % 20) * 8;
                Uint8 b = rand() % 40;

                // Size varies based on distance
                float size = 5 + (halfSize - distance / 5) / 5;

                filledCircleRGBA(renderer, cx, cy, size, r, g, b, 255);

                // Smaller bright center
                filledCircleRGBA(renderer, cx, cy, size / 2, 255, 230, 200, 255);
            }

            // Smoke particles
            for (int i = 0; i < 15; i++)
            {
                float angle = 2.0f * M_PI * i / 15.0f - explosionPhase;
                float distance = (halfSize - 5) * (1.2f + ((float)rand() / RAND_MAX) * 1.0f);
                float cx = x + cos(angle) * distance;
                float cy = y + sin(angle) * distance;

                // Gray smoke
                Uint8 gray = 40 + rand() % 60;

                filledCircleRGBA(renderer, cx, cy, 7 + rand() % 7, gray, gray, gray, 150);
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
    sliceSound = Mix_LoadWAV("assets/sounds/slice.wav");
    bombSound = Mix_LoadWAV("assets/sounds/bomb.wav");
    backgroundMusic = Mix_LoadMUS("assets/sounds/background.wav");

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

            // Fill with dark blue gradient (night sky)
            for (int y = 0; y < WINDOW_HEIGHT; y++)
            {
                // Gradient from dark blue to slightly lighter blue
                float gradientFactor = (float)y / WINDOW_HEIGHT;
                Uint8 r = 5 + (int)(15 * gradientFactor);
                Uint8 g = 5 + (int)(10 * gradientFactor);
                Uint8 b = 40 + (int)(20 * gradientFactor);

                SDL_SetRenderDrawColor(renderer, r, g, b, 255);
                SDL_RenderDrawLine(renderer, 0, y, WINDOW_WIDTH, y);
            }

            // Draw some stars with varying brightness
            srand(time(NULL));
            for (int i = 0; i < 200; i++)
            {
                int x = rand() % WINDOW_WIDTH;
                int y = rand() % WINDOW_HEIGHT;
                int brightness = 150 + rand() % 106;  // 150-255
                int size = (rand() % 10 > 8) ? 2 : 1; // Occasional larger stars

                SDL_SetRenderDrawColor(renderer, brightness, brightness, brightness, 255);
                SDL_Rect star = {x, y, size, size};
                SDL_RenderFillRect(renderer, &star);

                // Add occasional twinkle effect (brighter center)
                if (rand() % 10 == 0)
                {
                    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 200);
                    SDL_Rect twinkle = {x, y, 1, 1};
                    SDL_RenderFillRect(renderer, &twinkle);
                }
            }

            // Draw a few clouds
            for (int i = 0; i < 3; i++)
            {
                int cloudX = rand() % WINDOW_WIDTH;
                int cloudY = 50 + rand() % 150;
                int cloudSize = 30 + rand() % 60;

                for (int j = 0; j < 8; j++)
                {
                    int offsetX = (rand() % (cloudSize / 2)) - cloudSize / 4;
                    int offsetY = (rand() % (cloudSize / 3)) - cloudSize / 6;
                    int circleSize = cloudSize / 4 + rand() % (cloudSize / 3);

                    filledCircleRGBA(renderer,
                                     cloudX + offsetX,
                                     cloudY + offsetY,
                                     circleSize,
                                     30, 30, 50, 100);
                }
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

            // Check for slice based on mouse movement (no click required)
            if (abs(mouse_x - prev_mouse_x) > 3 || abs(mouse_y - prev_mouse_y) > 3)
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

                // Set mouse_down to true for rendering the slice trail
                mouse_down = 1;
            }
            else
            {
                // If mouse barely moved, don't show the slice trail
                mouse_down = 0;
            }
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

    // Create a nice-looking score panel
    for (int i = 0; i < 40; i++)
    {
        int alpha = 180 - i * 3;
        if (alpha < 0)
            alpha = 0;
        SDL_SetRenderDrawColor(renderer, 30, 30, 60, alpha);
        SDL_Rect scoreGradient = {10, 10 + i, 140, 1};
        SDL_RenderFillRect(renderer, &scoreGradient);
    }

    // Main score box
    SDL_SetRenderDrawColor(renderer, 30, 30, 60, 180);
    SDL_Rect scoreRect = {10, 10, 140, 40};
    SDL_RenderFillRect(renderer, &scoreRect);

    // Border for score box
    SDL_SetRenderDrawColor(renderer, 100, 100, 200, 255);
    SDL_Rect scoreBorder = {10, 10, 140, 40};
    SDL_RenderDrawRect(renderer, &scoreBorder);

    // Score text (since we don't have SDL_ttf loaded, we'll draw a stylized number)
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    char scoreStr[20];
    sprintf(scoreStr, "%d", score);

    // Draw "SCORE:" text
    int textX = 20;
    int textY = 17;

    // S
    SDL_Rect sRect[] = {
        {textX, textY, 3, 2},
        {textX, textY + 2, 3, 2},
        {textX, textY + 4, 3, 2},
        {textX + 3, textY, 3, 2},
        {textX + 3, textY + 4, 3, 2}};
    for (int i = 0; i < 5; i++)
    {
        SDL_RenderFillRect(renderer, &sRect[i]);
    }

    // Draw score number
    int digitWidth = 15;
    int digitX = 90;
    int digitY = 20;

    // Display score as a digital-style number
    for (int i = 0; scoreStr[i] != '\0'; i++)
    {
        int digit = scoreStr[i] - '0';

        // Base for all digits
        SDL_Rect digitBase = {digitX + i * digitWidth, digitY, 10, 20};

        // Draw segments based on which digit
        switch (digit)
        {
        case 0:
            SDL_RenderDrawRect(renderer, &digitBase);
            break;
        case 1:
            SDL_RenderDrawLine(renderer, digitX + i * digitWidth + 10, digitY,
                               digitX + i * digitWidth + 10, digitY + 20);
            break;
        case 2:
            SDL_RenderDrawLine(renderer, digitX + i * digitWidth, digitY,
                               digitX + i * digitWidth + 10, digitY);
            SDL_RenderDrawLine(renderer, digitX + i * digitWidth + 10, digitY,
                               digitX + i * digitWidth + 10, digitY + 10);
            SDL_RenderDrawLine(renderer, digitX + i * digitWidth, digitY + 10,
                               digitX + i * digitWidth + 10, digitY + 10);
            SDL_RenderDrawLine(renderer, digitX + i * digitWidth, digitY + 10,
                               digitX + i * digitWidth, digitY + 20);
            SDL_RenderDrawLine(renderer, digitX + i * digitWidth, digitY + 20,
                               digitX + i * digitWidth + 10, digitY + 20);
            break;
        default:
            // Simple digit display for others
            SDL_RenderDrawRect(renderer, &digitBase);
            SDL_RenderDrawLine(renderer, digitX + i * digitWidth, digitY + 10,
                               digitX + i * digitWidth + 10, digitY + 10);
        }
    }

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
        // Create dynamic slice trail
        static float trailOpacity[10] = {0}; // Opacity values for trailing segments
        static int trailX[10] = {0};         // X positions for trail segments
        static int trailY[10] = {0};         // Y positions for trail segments

        // Shift trail values
        for (int i = 9; i > 0; i--)
        {
            trailX[i] = trailX[i - 1];
            trailY[i] = trailY[i - 1];
            trailOpacity[i] = trailOpacity[i - 1] * 0.8f; // Fade out
        }

        // Add new point to trail
        trailX[0] = mouse_x;
        trailY[0] = mouse_y;
        trailOpacity[0] = 0.9f;

        // Draw trail with gradient
        for (int i = 1; i < 10; i++)
        {
            if (trailOpacity[i] > 0.05f)
            {
                int alpha = (int)(trailOpacity[i] * 255);
                int thickness = 3 - i / 3;
                if (thickness < 1)
                    thickness = 1;

                // White core with blue outer glow
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, alpha);
                SDL_RenderDrawLine(renderer, trailX[i - 1], trailY[i - 1], trailX[i], trailY[i]);

                // Thicker colored trail
                for (int t = 1; t <= thickness; t++)
                {
                    // Different colors for a rainbow effect
                    SDL_SetRenderDrawColor(renderer,
                                           100 + (i * 15),
                                           150 + ((i + 3) % 10) * 10,
                                           255,
                                           alpha / (t + 1));

                    // Draw parallel lines to create thickness
                    float angle = atan2(trailY[i] - trailY[i - 1], trailX[i] - trailX[i - 1]) + M_PI / 2;

                    int offsetX = (int)(cos(angle) * t);
                    int offsetY = (int)(sin(angle) * t);

                    SDL_RenderDrawLine(renderer,
                                       trailX[i - 1] + offsetX, trailY[i - 1] + offsetY,
                                       trailX[i] + offsetX, trailY[i] + offsetY);

                    SDL_RenderDrawLine(renderer,
                                       trailX[i - 1] - offsetX, trailY[i - 1] - offsetY,
                                       trailX[i] - offsetX, trailY[i] - offsetY);
                }

                // Add sparkle effects at intervals
                if (i % 3 == 0)
                {
                    int sparkleSize = 3 - i / 4;
                    if (sparkleSize > 0)
                    {
                        SDL_SetRenderDrawColor(renderer, 255, 255, 220, alpha);
                        SDL_Rect sparkle = {trailX[i] - sparkleSize / 2, trailY[i] - sparkleSize / 2,
                                            sparkleSize, sparkleSize};
                        SDL_RenderFillRect(renderer, &sparkle);
                    }
                }
            }
        }
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