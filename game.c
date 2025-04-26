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
#define MAX_FRUITS 203
#define FRUIT_TYPES 3 // Apple, Banana, Orange
#define BOMB_CHANCE 5 // 1 in 10 chance of spawning a bomb
#define FRUIT_SIZE 64
#define MAX_SCORES 10 // Maximum number of high scores to track

// Game states
typedef enum
{
    STATE_PLAYING,
    STATE_GAME_OVER,
    STATE_LEADERBOARD
} GameState;

// Score record for leaderboard
typedef struct
{
    int score;
    char date[20]; // Format: YYYY-MM-DD HH:MM:SS
} ScoreRecord;

// Deadlock detection constants
#define MAX_RESOURCES 4
#define MAX_PROCESSES 4

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

// Deadlock detection structures
typedef struct
{
    int allocation[MAX_PROCESSES][MAX_RESOURCES];
    int max_claim[MAX_PROCESSES][MAX_RESOURCES];
    int available[MAX_RESOURCES];
    int request[MAX_PROCESSES][MAX_RESOURCES];
    int work[MAX_RESOURCES];
    int finish[MAX_PROCESSES];
    int safe_sequence[MAX_PROCESSES];
    pthread_mutex_t deadlock_mutex;
    int deadlock_check_active;
} DeadlockDetector;

// Global variables
GameObject gameObjects[MAX_FRUITS];
pthread_mutex_t game_mutex;
int score = 0;
int health = 3;        // Player health (hearts)
int game_time = 0;     // Game timer in seconds
Uint32 start_time = 0; // Start time in milliseconds
int running = 1;
int spawn_pipe[2]; // Pipe for communicating with spawn process
GameState game_state = STATE_PLAYING;
ScoreRecord leaderboard[MAX_SCORES];
int num_scores = 0;

// Deadlock detection globals
DeadlockDetector deadlock_detector;
pthread_t deadlock_thread;
int resources_held[MAX_RESOURCES] = {0};
int resource_request_probability = 15; // 1 in 15 chance of resource request

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
int checkCollision(float slice_x, float slice_y, GameObject *obj);
int lineCircleIntersect(float line_x1, float line_y1, float line_x2, float line_y2, float circle_x, float circle_y, float radius);
void spawnFruit(int index);
void spawnFruitAt(int index, float x, float y, float vx, float vy);
void resetGame();
void loadScores();
void saveScores();
void addScore(int new_score);
void drawDigitalChar(SDL_Renderer *renderer, char c, int x, int y, int w, int h);
void drawDigitalText(SDL_Renderer *renderer, const char *text, int x, int y, int charWidth, int charHeight, int spacing);

// Initialize deadlock detector
void initDeadlockDetector()
{
    memset(&deadlock_detector, 0, sizeof(DeadlockDetector));
    pthread_mutex_init(&deadlock_detector.deadlock_mutex, NULL);

    // Initialize available resources
    for (int i = 0; i < MAX_RESOURCES; i++)
    {
        deadlock_detector.available[i] = 3 + rand() % 3; // 3-5 of each resource
    }

    // Initialize max claims for each process
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        for (int j = 0; j < MAX_RESOURCES; j++)
        {
            deadlock_detector.max_claim[i][j] = rand() % 3; // 0-2 of each resource
        }
    }

    deadlock_detector.deadlock_check_active = 0;
}

// Clean up deadlock detector resources
void cleanupDeadlockDetector()
{
    pthread_mutex_destroy(&deadlock_detector.deadlock_mutex);
}

// Resource allocation function
int requestResource(int process_id, int resource_id, int amount)
{
    pthread_mutex_lock(&deadlock_detector.deadlock_mutex);

    // Check if the request exceeds max claim
    if (deadlock_detector.allocation[process_id][resource_id] + amount >
        deadlock_detector.max_claim[process_id][resource_id])
    {
        pthread_mutex_unlock(&deadlock_detector.deadlock_mutex);
        return -1; // Request exceeds maximum claim
    }

    // Check if enough resources are available
    if (amount > deadlock_detector.available[resource_id])
    {
        // Record the request
        deadlock_detector.request[process_id][resource_id] = amount;
        pthread_mutex_unlock(&deadlock_detector.deadlock_mutex);
        return 0; // Resource not available, process must wait
    }

    // Allocate the resource
    deadlock_detector.allocation[process_id][resource_id] += amount;
    deadlock_detector.available[resource_id] -= amount;
    deadlock_detector.request[process_id][resource_id] = 0;

    pthread_mutex_unlock(&deadlock_detector.deadlock_mutex);
    return 1; // Resource allocated successfully
}

// Release allocated resources
void releaseResource(int process_id, int resource_id, int amount)
{
    pthread_mutex_lock(&deadlock_detector.deadlock_mutex);

    if (deadlock_detector.allocation[process_id][resource_id] < amount)
    {
        // This shouldn't happen in a correct implementation
        printf("Warning: Trying to release more resources than allocated\n");
        amount = deadlock_detector.allocation[process_id][resource_id];
    }

    deadlock_detector.allocation[process_id][resource_id] -= amount;
    deadlock_detector.available[resource_id] += amount;

    pthread_mutex_unlock(&deadlock_detector.deadlock_mutex);
}

// Deadlock detection algorithm (Banker's algorithm)
int detectDeadlock()
{
    pthread_mutex_lock(&deadlock_detector.deadlock_mutex);

    // If detection is already running, don't start another
    if (deadlock_detector.deadlock_check_active)
    {
        pthread_mutex_unlock(&deadlock_detector.deadlock_mutex);
        return -1;
    }

    deadlock_detector.deadlock_check_active = 1;

    // Initialize work and finish arrays
    for (int i = 0; i < MAX_RESOURCES; i++)
    {
        deadlock_detector.work[i] = deadlock_detector.available[i];
    }

    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        deadlock_detector.finish[i] = 0;
    }

    // Find an unfinished process whose needs can be satisfied
    int found;
    int deadlock_detected = 0;
    int safe_index = 0;

    do
    {
        found = 0;
        for (int i = 0; i < MAX_PROCESSES; i++)
        {
            if (deadlock_detector.finish[i] == 0)
            {
                int j;
                for (j = 0; j < MAX_RESOURCES; j++)
                {
                    if (deadlock_detector.max_claim[i][j] - deadlock_detector.allocation[i][j] >
                        deadlock_detector.work[j])
                    {
                        break;
                    }
                }

                if (j == MAX_RESOURCES)
                {
                    // This process can finish
                    for (int k = 0; k < MAX_RESOURCES; k++)
                    {
                        deadlock_detector.work[k] += deadlock_detector.allocation[i][k];
                    }
                    deadlock_detector.finish[i] = 1;
                    deadlock_detector.safe_sequence[safe_index++] = i;
                    found = 1;
                }
            }
        }
    } while (found);

    // Check if all processes are finished
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (deadlock_detector.finish[i] == 0)
        {
            deadlock_detected = 1;
            break;
        }
    }

    deadlock_detector.deadlock_check_active = 0;
    pthread_mutex_unlock(&deadlock_detector.deadlock_mutex);

    return deadlock_detected;
}

// Deadlock recovery function
void recoverFromDeadlock()
{
    pthread_mutex_lock(&deadlock_detector.deadlock_mutex);

    printf("Deadlock detected! Recovering...\n");

    // Simple recovery: release some resources from a deadlocked process
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (deadlock_detector.finish[i] == 0)
        {
            for (int j = 0; j < MAX_RESOURCES; j++)
            {
                if (deadlock_detector.allocation[i][j] > 0)
                {
                    // Release one resource
                    deadlock_detector.allocation[i][j]--;
                    deadlock_detector.available[j]++;
                    printf("Released resource %d from process %d\n", j, i);
                    break;
                }
            }
            break; // Only recover from one process at a time
        }
    }

    pthread_mutex_unlock(&deadlock_detector.deadlock_mutex);
}

// Deadlock thread function
void *deadlockMonitor(void *arg)
{
    (void)arg; // Unused parameter

    while (running)
    {
        // Simulate resource requests at random intervals
        if (rand() % resource_request_probability == 0)
        {
            int process_id = rand() % MAX_PROCESSES;
            int resource_id = rand() % MAX_RESOURCES;
            int amount = 1 + rand() % 2; // Request 1-2 resources

            int result = requestResource(process_id, resource_id, amount);
            if (result == 1)
            {
                printf("Process %d acquired %d of resource %d\n",
                       process_id, amount, resource_id);
            }
        }

        // Simulate resource releases at random intervals
        if (rand() % (resource_request_probability * 2) == 0)
        {
            int process_id = rand() % MAX_PROCESSES;
            int resource_id = rand() % MAX_RESOURCES;

            if (deadlock_detector.allocation[process_id][resource_id] > 0)
            {
                int amount = 1;
                releaseResource(process_id, resource_id, amount);
                printf("Process %d released %d of resource %d\n",
                       process_id, amount, resource_id);
            }
        }

        // Run deadlock detection periodically
        if (rand() % (resource_request_probability * 3) == 0)
        {
            int deadlock = detectDeadlock();
            if (deadlock == 1)
            {
                recoverFromDeadlock();
            }
        }

        // Sleep to prevent excessive CPU usage
        usleep(100000); // 100ms
    }

    return NULL;
}

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
            // Sliced banana with more detailed inside - enhanced appearance
            SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);

            // Separation gap between banana halves
            float separationX = 16.0f; // Increased from 15 for more visible separation

            // Left half - more curved for better visual slicing
            for (int i = -10; i <= 0; i++)
            {
                float angle = (float)i / 10.0f * 3.14f;
                float cx = x - separationX + cos(angle + rotation) * halfSize * 0.7f;
                float cy = y + sin(angle + rotation) * halfSize * 0.3f;
                filledCircleRGBA(renderer, cx, cy, 6, 255, 255, 30, 255);
            }

            // Right half - more curved for better visual slicing
            for (int i = 0; i <= 10; i++)
            {
                float angle = (float)i / 10.0f * 3.14f;
                float cx = x + separationX + cos(angle + rotation) * halfSize * 0.7f;
                float cy = y + sin(angle + rotation) * halfSize * 0.3f;
                filledCircleRGBA(renderer, cx, cy, 6, 255, 255, 30, 255);
            }

            // Inside (creamy white) - more visible and contrasting
            for (int i = -8; i <= 0; i++)
            {
                float angle = (float)i / 8.0f * 3.14f;
                float cx = x - separationX + cos(angle + rotation) * halfSize * 0.5f;
                float cy = y + sin(angle + rotation) * halfSize * 0.2f;
                filledCircleRGBA(renderer, cx, cy, 5, 255, 250, 220, 255); // Bigger inside (was 4)
            }

            for (int i = 0; i <= 8; i++)
            {
                float angle = (float)i / 8.0f * 3.14f;
                float cx = x + separationX + cos(angle + rotation) * halfSize * 0.5f;
                float cy = y + sin(angle + rotation) * halfSize * 0.2f;
                filledCircleRGBA(renderer, cx, cy, 5, 255, 250, 220, 255); // Bigger inside (was 4)
            }

            // Seeds - darker and more visible
            SDL_SetRenderDrawColor(renderer, 20, 20, 0, 255); // Darker seeds (was 30, 30, 0)
            for (int i = -2; i <= 2; i++)
            {
                SDL_Rect seed1 = {x - separationX + i * 5, y, 3, 3}; // Bigger seeds
                SDL_Rect seed2 = {x + separationX + i * 5, y, 3, 3}; // Bigger seeds
                SDL_RenderFillRect(renderer, &seed1);
                SDL_RenderFillRect(renderer, &seed2);
            }

            // Draw slice "cut line" for more obvious slice effect
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 180);
            for (int i = -2; i <= 2; i++)
            {
                SDL_RenderDrawLine(renderer,
                                   x - halfSize + 10, y + i,
                                   x + halfSize - 10, y + i);
            }
        }
        break;

    case ORANGE:
        if (!sliced)
        {
            // Orange with texture and gradient - adjusted to match collision radius of FRUIT_SIZE * 0.55f
            int orangeRadius = (int)(halfSize * 0.85f); // Approximately 55% of FRUIT_SIZE (halfSize is FRUIT_SIZE/2)

            // Main orange body - more consistent size
            filledCircleRGBA(renderer, x + halfSize / 2, y + halfSize / 2, orangeRadius, 255, 140, 0, 255);
            filledCircleRGBA(renderer, x + halfSize / 2, y + halfSize / 2, orangeRadius - 3, 255, 165, 0, 255);

            // Subtle highlight (much smaller than before)
            filledCircleRGBA(renderer,
                             x + halfSize / 2 - orangeRadius / 4,
                             y + halfSize / 2 - orangeRadius / 4,
                             orangeRadius / 8, 255, 230, 180, 150);

            // Texture dots
            SDL_SetRenderDrawColor(renderer, 200, 120, 0, 255);
            for (int i = 0; i < 20; i++)
            {
                float angle = 2.0f * 3.14f * i / 20.0f + rotation;
                float radius = orangeRadius - 5 - (rand() % 5);
                float cx = x + halfSize / 2 + cos(angle) * radius;
                float cy = y + halfSize / 2 + sin(angle) * radius;
                filledCircleRGBA(renderer, cx, cy, 2, 220, 140, 0, 200);
            }

            // Stem/leaf detail at top
            SDL_SetRenderDrawColor(renderer, 50, 100, 0, 255);
            SDL_Rect stem = {x + halfSize / 2 - 4, y + halfSize / 2 - orangeRadius - 2, 8, 6};
            SDL_RenderFillRect(renderer, &stem);

            // Small leaf
            SDL_SetRenderDrawColor(renderer, 0, 130, 0, 255);
            SDL_Point leaf[3] = {
                {x + halfSize / 2, y + halfSize / 2 - orangeRadius + 1},
                {x + halfSize / 2 + 10, y + halfSize / 2 - orangeRadius - 4},
                {x + halfSize / 2 + 5, y + halfSize / 2 - orangeRadius + 4}};
            SDL_RenderDrawLines(renderer, leaf, 3);
        }
        else
        {
            // Sliced orange with detailed segments - enhanced separation
            float separationX = 18.0f;                  // Increased from 15 for more visible separation
            int orangeRadius = (int)(halfSize * 0.85f); // Match the unsliced radius

            // Outer rind - brighter color
            filledCircleRGBA(renderer, x + halfSize / 2 - separationX, y + halfSize / 2, orangeRadius - 5, 255, 140, 0, 255);
            filledCircleRGBA(renderer, x + halfSize / 2 + separationX, y + halfSize / 2, orangeRadius - 5, 255, 140, 0, 255);

            // White pith layer - more contrast
            filledCircleRGBA(renderer, x + halfSize / 2 - separationX, y + halfSize / 2, orangeRadius - 7, 255, 240, 220, 255); // Whiter
            filledCircleRGBA(renderer, x + halfSize / 2 + separationX, y + halfSize / 2, orangeRadius - 7, 255, 240, 220, 255); // Whiter

            // Inside pulp - more vibrant
            filledCircleRGBA(renderer, x + halfSize / 2 - separationX, y + halfSize / 2, orangeRadius - 10, 255, 160, 80, 255); // More orange (was 180, 100)
            filledCircleRGBA(renderer, x + halfSize / 2 + separationX, y + halfSize / 2, orangeRadius - 10, 255, 160, 80, 255); // More orange

            // Segment lines with thickness - more visible segments
            SDL_SetRenderDrawColor(renderer, 255, 220, 180, 255); // Brighter lines
            for (int i = 0; i < 8; i++)
            {
                float angle = 2.0f * M_PI * i / 8.0f;
                for (int w = -2; w <= 2; w++) // Wider lines (was -1 to 1)
                {
                    SDL_RenderDrawLine(renderer,
                                       x + halfSize / 2 - separationX, y + halfSize / 2,
                                       x + halfSize / 2 - separationX + cos(angle + w * 0.05) * (orangeRadius - 10),
                                       y + halfSize / 2 + sin(angle + w * 0.05) * (orangeRadius - 10));

                    SDL_RenderDrawLine(renderer,
                                       x + halfSize / 2 + separationX, y + halfSize / 2,
                                       x + halfSize / 2 + separationX + cos(angle + w * 0.05) * (orangeRadius - 10),
                                       y + halfSize / 2 + sin(angle + w * 0.05) * (orangeRadius - 10));
                }
            }

            // Seeds at center - more visible
            SDL_SetRenderDrawColor(renderer, 255, 240, 200, 255);
            filledCircleRGBA(renderer, x + halfSize / 2 - separationX, y + halfSize / 2, 6, 255, 240, 200, 255);
            filledCircleRGBA(renderer, x + halfSize / 2 + separationX, y + halfSize / 2, 6, 255, 240, 200, 255);

            // Individual seeds - more prominent
            SDL_SetRenderDrawColor(renderer, 200, 160, 50, 255);
            for (int i = 0; i < 5; i++)
            {
                float angle = 2.0f * M_PI * i / 5.0f;
                SDL_Rect seed1 = {
                    x + halfSize / 2 - separationX + cos(angle) * 3 - 1,
                    y + halfSize / 2 + sin(angle) * 3 - 1,
                    3, 4}; // Bigger (was 2, 3)
                SDL_Rect seed2 = {
                    x + halfSize / 2 + separationX + cos(angle) * 3 - 1,
                    y + halfSize / 2 + sin(angle) * 3 - 1,
                    3, 4}; // Bigger
                SDL_RenderFillRect(renderer, &seed1);
                SDL_RenderFillRect(renderer, &seed2);
            }

            // Draw slice "cut line" for more obvious slice effect
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 180);
            for (int i = -2; i <= 2; i++)
            {
                SDL_RenderDrawLine(renderer,
                                   x, y + halfSize / 2 + i,
                                   x + FRUIT_SIZE, y + halfSize / 2 + i);
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

    // Initialize game variables
    score = 0;
    health = 3;
    game_time = 0;
    start_time = SDL_GetTicks();

    // Play background music
    Mix_PlayMusic(backgroundMusic, -1);

    // Initialize deadlock detection system
    initDeadlockDetector();

    // Create deadlock monitoring thread
    if (pthread_create(&deadlock_thread, NULL, deadlockMonitor, NULL) != 0)
    {
        fprintf(stderr, "Failed to create deadlock monitoring thread\n");
        return 0;
    }

    // Load scores from file
    loadScores();

    return 1; // Success
}

// Spawn objects thread function
void *spawnObjects(void *arg)
{
    // Avoid unused parameter warning
    (void)arg;

    // Initialize random seed
    srand(time(NULL));

    // Variables for controlling spawn patterns
    int spawn_mode = 0; // 0 = regular, 1 = cluster, 2 = line, 3 = arc
    int spawn_timer = 0;
    int spawn_cooldown = 0;
    int last_spawn_time = 0; // Track when we last spawned something

    while (running)
    {
        // Lock mutex before modifying shared data
        pthread_mutex_lock(&game_mutex);

        // Get current time
        int current_time = SDL_GetTicks() / 1000;

        // Decrease spawn cooldown
        if (spawn_cooldown > 0)
        {
            spawn_cooldown--;
        }

        // Change spawn mode occasionally
        spawn_timer++;
        if (spawn_timer > 200)
        { // About every 10 seconds
            spawn_timer = 0;
            int prev_mode = spawn_mode;
            spawn_mode = rand() % 4; // Select a random spawn pattern
            printf("Spawn mode changed to: %d\n", spawn_mode);

            // If we're changing modes and there are few active fruits, force a spawn in the new mode
            if (prev_mode != spawn_mode)
            {
                // Count active fruits
                int active_count = 0;
                for (int i = 0; i < MAX_FRUITS; i++)
                {
                    if (gameObjects[i].active)
                    {
                        active_count++;
                    }
                }

                // If we have less than 3 active fruits, force an immediate spawn
                if (active_count < 3)
                {
                    spawn_cooldown = 0;  // Reset cooldown to allow immediate spawn
                    last_spawn_time = 0; // Reset last spawn time to force spawn
                }
            }
        }

        // Count active fruits
        int active_count = 0;
        for (int i = 0; i < MAX_FRUITS; i++)
        {
            if (gameObjects[i].active)
            {
                active_count++;
            }
        }

        // Check if we need an emergency spawn - if no active fruits or it's been too long
        bool need_emergency_spawn = (active_count == 0 || (current_time - last_spawn_time > 2));

        // Regular fruit spawning when cooldown is over
        if (spawn_cooldown <= 0 || need_emergency_spawn)
        {
            // Only spawn if we're below the limit
            if (active_count < MAX_FRUITS - 3) // Leave room for clusters
            {
                bool spawned_something = false;

                if (spawn_mode == 0) // Regular spawning
                {
                    // Find an inactive object slot
                    for (int i = 0; i < MAX_FRUITS; i++)
                    {
                        if (!gameObjects[i].active)
                        {
                            // Random chance to spawn - reduced spawn rate
                            if (need_emergency_spawn || rand() % 15 == 0) // Was 20 (more frequent now)
                            {
                                spawnFruit(i);
                                spawn_cooldown = 2; // Reduced cooldown (was 3)
                                spawned_something = true;
                                last_spawn_time = current_time;
                                break;
                            }
                        }
                    }
                }
                else if (spawn_mode == 1 && active_count < MAX_FRUITS - 5) // Cluster spawning
                {
                    // Spawn a cluster of 3-5 fruits close together - less frequently
                    if (need_emergency_spawn || rand() % 30 == 0) // Was 40 (more frequent now)
                    {
                        int cluster_size = 3 + rand() % 3; // 3-5 fruits
                        int base_x = 100 + rand() % (WINDOW_WIDTH - 200);
                        float base_vx = -3.0f + (rand() % 60) / 10.0f;
                        float base_vy = 2.0f + (rand() % 30) / 10.0f;

                        int spawned = 0;
                        for (int i = 0; i < MAX_FRUITS && spawned < cluster_size; i++)
                        {
                            if (!gameObjects[i].active)
                            {
                                spawnFruitAt(i,
                                             base_x + (rand() % 120 - 60),
                                             -(rand() % 20),
                                             base_vx + ((rand() % 20 - 10) / 10.0f),
                                             base_vy + ((rand() % 20) / 10.0f));
                                spawned++;
                            }
                        }
                        spawn_cooldown = 25; // Reduced cooldown (was 40)
                        spawned_something = true;
                        last_spawn_time = current_time;
                    }
                }
                else if (spawn_mode == 2) // Line formation
                {
                    // Spawn fruits in a horizontal line for slicing swipes - less frequently
                    if (need_emergency_spawn || rand() % 35 == 0) // Was 50 (more frequent now)
                    {
                        int line_count = 4 + rand() % 3; // 4-6 fruits
                        int spacing = FRUIT_SIZE + 10;
                        int start_x = (WINDOW_WIDTH - line_count * spacing) / 2 + rand() % 100 - 50;
                        int y_pos = -30;
                        float shared_vx = -1.0f + (rand() % 20) / 10.0f;
                        float shared_vy = 2.0f + (rand() % 20) / 10.0f;

                        int spawned = 0;
                        for (int i = 0; i < MAX_FRUITS && spawned < line_count; i++)
                        {
                            if (!gameObjects[i].active)
                            {
                                spawnFruitAt(i,
                                             start_x + spacing * spawned,
                                             y_pos + (rand() % 20 - 10),
                                             shared_vx,
                                             shared_vy);
                                spawned++;
                            }
                        }
                        spawn_cooldown = 30; // Reduced cooldown (was 50)
                        spawned_something = true;
                        last_spawn_time = current_time;
                    }
                }
                else if (spawn_mode == 3) // Arc formation
                {
                    // Spawn fruits in an arc pattern - less frequently
                    if (need_emergency_spawn || rand() % 60 == 0) // Was 100, now 60 (more frequent)
                    {
                        int arc_count = 5 + rand() % 3; // 5-7 fruits
                        float arc_radius = 100.0f + rand() % 50;
                        float arc_center_x = WINDOW_WIDTH / 2 + rand() % 200 - 100;
                        float arc_start = -M_PI / 4 - (rand() % 20) / 100.0f;
                        float arc_end = M_PI / 4 + (rand() % 20) / 100.0f;
                        float arc_step = (arc_end - arc_start) / (arc_count - 1);

                        float shared_vy = 3.0f + (rand() % 20) / 10.0f;

                        int spawned = 0;
                        for (int i = 0; i < MAX_FRUITS && spawned < arc_count; i++)
                        {
                            if (!gameObjects[i].active)
                            {
                                float angle = arc_start + arc_step * spawned;
                                float x_pos = arc_center_x + cos(angle) * arc_radius;
                                float y_pos = -30;
                                float vx = sin(angle) * 2.0f;

                                spawnFruitAt(i, x_pos, y_pos, vx, shared_vy);
                                spawned++;
                            }
                        }
                        spawn_cooldown = 60; // Reduced cooldown (was 110)
                        spawned_something = true;
                        last_spawn_time = current_time;
                    }
                }

                // Emergency spawn if nothing was spawned and we need to
                if (need_emergency_spawn && !spawned_something && active_count == 0)
                {
                    // Guaranteed spawn at least one fruit
                    for (int i = 0; i < MAX_FRUITS; i++)
                    {
                        if (!gameObjects[i].active)
                        {
                            spawnFruit(i);
                            last_spawn_time = current_time;
                            break;
                        }
                    }
                }
            }
        }

        // Unlock mutex
        pthread_mutex_unlock(&game_mutex);

        // Sleep to control spawn rate (increased to slow down spawn rate)
        usleep(100000); // 100ms instead of 40ms
    }

    return NULL;
}

// Helper function to spawn a fruit with default parameters
void spawnFruit(int index)
{
    gameObjects[index].active = 1;
    gameObjects[index].x = rand() % (WINDOW_WIDTH - FRUIT_SIZE);
    gameObjects[index].y = 0; // Drop from the top of the screen

    // Random horizontal velocity component with increased speed
    gameObjects[index].vx = (-3.0f + (rand() % 60) / 10.0f) * 1.5f; // 50% faster (was 1.3f)
    gameObjects[index].vy = (2.0f + (rand() % 30) / 10.0f) * 1.5f;  // 50% faster (was 1.3f)

    gameObjects[index].sliced = 0;
    gameObjects[index].rotation = 0.0f;
    gameObjects[index].rotSpeed = (0.05f + ((float)rand() / RAND_MAX) * 0.1f) * 1.5f; // Faster rotation (was 1.2f)
    if (rand() % 2)
        gameObjects[index].rotSpeed *= -1; // Random direction

    // Determine if it's a bomb or fruit
    if (rand() % BOMB_CHANCE == 0)
    {
        gameObjects[index].type = BOMB;
    }
    else
    {
        gameObjects[index].type = rand() % FRUIT_TYPES;
    }

    // Initialize slice pieces (will be used when sliced)
    for (int j = 0; j < SLICE_PIECES; j++)
    {
        gameObjects[index].pieces[j].timeLeft = 0;
    }
}

// Helper function to spawn a fruit at specific position with specific velocity
void spawnFruitAt(int index, float x, float y, float vx, float vy)
{
    gameObjects[index].active = 1;
    gameObjects[index].x = x;
    gameObjects[index].y = y;

    // Apply speed multiplier
    gameObjects[index].vx = vx * 1.5f; // 50% faster (was 1.3f)
    gameObjects[index].vy = vy * 1.5f; // 50% faster (was 1.3f)

    gameObjects[index].sliced = 0;
    gameObjects[index].rotation = 0.0f;
    gameObjects[index].rotSpeed = (0.05f + ((float)rand() / RAND_MAX) * 0.1f) * 1.5f; // Faster rotation (was 1.2f)
    if (rand() % 2)
        gameObjects[index].rotSpeed *= -1; // Random direction

    // Determine if it's a bomb or fruit (less bombs in formations)
    if (rand() % (BOMB_CHANCE * 2) == 0) // Half as many bombs in formations
    {
        gameObjects[index].type = BOMB;
    }
    else
    {
        gameObjects[index].type = rand() % FRUIT_TYPES;
    }

    // Initialize slice pieces (will be used when sliced)
    for (int j = 0; j < SLICE_PIECES; j++)
    {
        gameObjects[index].pieces[j].timeLeft = 0;
    }
}

// Function to detect line segment intersection with circle
// Used to detect if the slice path intersects a fruit
int lineCircleIntersect(float line_x1, float line_y1, float line_x2, float line_y2, float circle_x, float circle_y, float radius)
{
    // Vector from line start to circle center
    float dx = circle_x - line_x1;
    float dy = circle_y - line_y1;

    // Vector of the line
    float line_dx = line_x2 - line_x1;
    float line_dy = line_y2 - line_y1;

    // Length squared of the line segment
    float line_len_sq = line_dx * line_dx + line_dy * line_dy;

    // Exit early if line has zero length
    if (line_len_sq == 0)
    {
        return dx * dx + dy * dy <= radius * radius;
    }

    // Calculate projection of circle center onto line
    float t = (dx * line_dx + dy * line_dy) / line_len_sq;

    // Clamp t to [0,1] for line segment
    if (t < 0)
        t = 0;
    if (t > 1)
        t = 1;

    // Find closest point on line segment to circle center
    float closest_x = line_x1 + t * line_dx;
    float closest_y = line_y1 + t * line_dy;

    // Check if this point is within the circle
    float dist_x = closest_x - circle_x;
    float dist_y = closest_y - circle_y;
    float dist_sq = dist_x * dist_x + dist_y * dist_y;

    return dist_sq <= radius * radius;
}

// Improved collision detection function to account for velocity
int checkCollision(float slice_x, float slice_y, GameObject *obj)
{
    // Get center coordinates and boundaries
    float center_x = obj->x + FRUIT_SIZE / 2;
    float center_y = obj->y + FRUIT_SIZE / 2;

    // Special case for banana - use an elongated box along its curve
    if (obj->type == BANANA)
    {
        // Use a wider but shorter box for banana due to its curved shape
        float banana_box_width = FRUIT_SIZE * 1.6f;
        float banana_box_height = FRUIT_SIZE * 0.8f;

        // The banana's curve means we need to offset the box based on rotation
        float box_offset_x = cos(obj->rotation) * FRUIT_SIZE * 0.2f;
        float box_offset_y = sin(obj->rotation) * FRUIT_SIZE * 0.1f;

        float box_left = center_x - banana_box_width / 2 + box_offset_x;
        float box_top = center_y - banana_box_height / 2 + box_offset_y;

        // Check if the slice point is within the banana's elongated box
        if (slice_x >= box_left && slice_x <= box_left + banana_box_width &&
            slice_y >= box_top && slice_y <= box_top + banana_box_height)
        {
            return 1; // Hit!
        }
    }
    // Special case for orange - more spherical, so use a more accurate circle
    else if (obj->type == ORANGE)
    {
        // Calculate distance from slice point to orange center
        float dx = slice_x - center_x;
        float dy = slice_y - center_y;
        float distance_squared = dx * dx + dy * dy;

        // Oranges should have a slightly larger hit radius than other fruits
        float orange_radius = FRUIT_SIZE * 0.55f;

        if (distance_squared < orange_radius * orange_radius)
        {
            return 1; // Hit!
        }
    }

    // For other fruits, use the standard box collision first
    // Box collision detection - using more generous box for banana and orange
    float box_scale = (obj->type == BANANA || obj->type == ORANGE) ? 1.3f : 1.2f;
    float box_left = obj->x - (FRUIT_SIZE * (box_scale - 1.0f) / 2);
    float box_top = obj->y - (FRUIT_SIZE * (box_scale - 1.0f) / 2);
    float box_width = FRUIT_SIZE * box_scale;
    float box_height = FRUIT_SIZE * box_scale;

    // Box collision check
    if (slice_x >= box_left && slice_x <= box_left + box_width &&
        slice_y >= box_top && slice_y <= box_top + box_height)
    {
        return 1; // Hit!
    }

    // If box collision failed, try circle collision as a backup
    // This helps with curved shapes and other fruits

    // Calculate distance from slice point to fruit center
    float dx = slice_x - center_x;
    float dy = slice_y - center_y;
    float distance_squared = dx * dx + dy * dy;

    // Very generous hit radius - almost the entire fruit area
    float hit_radius;
    switch (obj->type)
    {
    case APPLE:
        hit_radius = FRUIT_SIZE * 0.6f;
        break;
    case ORANGE:
        hit_radius = FRUIT_SIZE * 0.55f; // Changed from 0.65f to match orangeRadius defined elsewhere
        break;
    case BANANA:
        hit_radius = FRUIT_SIZE * 0.75f; // Increased from 0.7f
        break;
    case BOMB:
        hit_radius = FRUIT_SIZE * 0.5f;
        break;
    default:
        hit_radius = FRUIT_SIZE * 0.6f;
    }

    // Check simple circle collision
    if (distance_squared < hit_radius * hit_radius)
    {
        return 1; // Hit!
    }

    // For fast-moving fruits, create a velocity-based adjustment
    // This helps with hitting fruits that might have moved between frames
    float velocity_magnitude = sqrt(obj->vx * obj->vx + obj->vy * obj->vy);

    // If the fruit is moving fast, increase the hit radius even more
    if (velocity_magnitude > 5.0f)
    {
        // Increase hit radius based on velocity - more for banana and orange
        float speed_bonus = (obj->type == BANANA || obj->type == ORANGE) ? 0.25f : 0.2f;
        hit_radius += velocity_magnitude * speed_bonus;

        // Also check slightly ahead of the fruit's position based on its direction
        // This helps with hitting fruits that appear to be ahead of their hitbox
        float vel_dx = slice_x - (center_x + obj->vx * 0.15f);
        float vel_dy = slice_y - (center_y + obj->vy * 0.15f);
        float vel_distance_squared = vel_dx * vel_dx + vel_dy * vel_dy;

        // Return true if future position check succeeds
        if (vel_distance_squared < hit_radius * hit_radius)
        {
            return 1;
        }
    }

    // Return false as all tests failed
    return 0;
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
            // Store previous position before updating current
            prev_mouse_x = mouse_x;
            prev_mouse_y = mouse_y;

            // Update current mouse position
            mouse_x = e.motion.x;
            mouse_y = e.motion.y;

            // Only process mouse movement for slicing if we're in the PLAYING state
            if (game_state == STATE_PLAYING)
            {
                // Improve slice detection - check if mouse moved fast enough to count as a slice
                float mouse_movement = sqrt(pow(mouse_x - prev_mouse_x, 2) + pow(mouse_y - prev_mouse_y, 2));

                // Only count as a slice if the movement is significant
                if (mouse_movement > 5)
                {
                    pthread_mutex_lock(&game_mutex);

                    // Track which objects were sliced to avoid double-counting
                    int sliced_objects[MAX_FRUITS] = {0};

                    // First check if the line formed by mouse movement intersects any fruit
                    for (int i = 0; i < MAX_FRUITS; i++)
                    {
                        if (gameObjects[i].active && !gameObjects[i].sliced && !sliced_objects[i])
                        {
                            float center_x = gameObjects[i].x + FRUIT_SIZE / 2;
                            float center_y = gameObjects[i].y + FRUIT_SIZE / 2;

                            // Use a generous radius for line intersection test - larger for bananas and oranges
                            float hit_radius;
                            if (gameObjects[i].type == BANANA)
                            {
                                // Bananas need a wider hit area due to their elongated shape
                                hit_radius = FRUIT_SIZE * 0.8f;

                                // For bananas, also check with an offset based on rotation to account for its curve
                                float offset_x = cos(gameObjects[i].rotation) * FRUIT_SIZE * 0.2f;
                                float offset_y = sin(gameObjects[i].rotation) * FRUIT_SIZE * 0.1f;

                                // Check both the center and the offset points
                                if (lineCircleIntersect(prev_mouse_x, prev_mouse_y, mouse_x, mouse_y,
                                                        center_x + offset_x, center_y + offset_y, hit_radius) ||
                                    lineCircleIntersect(prev_mouse_x, prev_mouse_y, mouse_x, mouse_y,
                                                        center_x, center_y, hit_radius))
                                {
                                    // Fruit hit by slice line!
                                    gameObjects[i].sliced = 1;
                                    sliced_objects[i] = 1;

                                    // Initialize slice pieces
                                    float sliceAngle = atan2(mouse_y - prev_mouse_y, mouse_x - prev_mouse_x);

                                    // Create two pieces moving in different directions
                                    for (int j = 0; j < SLICE_PIECES; j++)
                                    {
                                        gameObjects[i].pieces[j].x = center_x;
                                        gameObjects[i].pieces[j].y = center_y;

                                        // Different velocities for each piece
                                        float pieceAngle = sliceAngle + (j == 0 ? M_PI / 2 : -M_PI / 2);
                                        float speed = (2.0f + (rand() % 20) / 10.0f) * 1.5f; // 50% faster

                                        gameObjects[i].pieces[j].vx = cos(pieceAngle) * speed;
                                        gameObjects[i].pieces[j].vy = sin(pieceAngle) * speed + gameObjects[i].vy / 2;
                                        gameObjects[i].pieces[j].rotation = gameObjects[i].rotation;
                                        gameObjects[i].pieces[j].rotSpeed = gameObjects[i].rotSpeed * 2.0f * (j == 0 ? 1 : -1);
                                        gameObjects[i].pieces[j].timeLeft = SLICE_DURATION;
                                    }

                                    // Play slice sound
                                    Mix_PlayChannel(-1, sliceSound, 0);
                                    score += 1;
                                    printf("Banana sliced! Score: %d\n", score);
                                }
                            }
                            else if (gameObjects[i].type == ORANGE)
                            {
                                // Oranges can have a larger radius since they're round
                                hit_radius = FRUIT_SIZE * 0.55f; // Match exactly with orangeRadius in rendering (0.85f of halfSize = 0.55f of FRUIT_SIZE)

                                if (lineCircleIntersect(prev_mouse_x, prev_mouse_y, mouse_x, mouse_y,
                                                        center_x, center_y, hit_radius))
                                {
                                    // Orange hit by slice line!
                                    gameObjects[i].sliced = 1;
                                    sliced_objects[i] = 1;

                                    // Initialize slice pieces
                                    float sliceAngle = atan2(mouse_y - prev_mouse_y, mouse_x - prev_mouse_x);

                                    // Create two pieces moving in different directions
                                    for (int j = 0; j < SLICE_PIECES; j++)
                                    {
                                        gameObjects[i].pieces[j].x = center_x;
                                        gameObjects[i].pieces[j].y = center_y;

                                        // Different velocities for each piece
                                        float pieceAngle = sliceAngle + (j == 0 ? M_PI / 2 : -M_PI / 2);
                                        float speed = (2.0f + (rand() % 20) / 10.0f) * 1.5f; // 50% faster

                                        gameObjects[i].pieces[j].vx = cos(pieceAngle) * speed;
                                        gameObjects[i].pieces[j].vy = sin(pieceAngle) * speed + gameObjects[i].vy / 2;
                                        gameObjects[i].pieces[j].rotation = gameObjects[i].rotation;
                                        gameObjects[i].pieces[j].rotSpeed = gameObjects[i].rotSpeed * 2.0f * (j == 0 ? 1 : -1);
                                        gameObjects[i].pieces[j].timeLeft = SLICE_DURATION;
                                    }

                                    // Play slice sound
                                    Mix_PlayChannel(-1, sliceSound, 0);
                                    score += 1;
                                    printf("Orange sliced! Score: %d\n", score);
                                }
                            }
                            else
                            {
                                // For other fruit types and bombs
                                hit_radius = FRUIT_SIZE * 0.7f;

                                // Check current movement path
                                if (lineCircleIntersect(prev_mouse_x, prev_mouse_y, mouse_x, mouse_y,
                                                        center_x, center_y, hit_radius))
                                {
                                    // Fruit hit by slice line!
                                    gameObjects[i].sliced = 1;
                                    sliced_objects[i] = 1;

                                    // Initialize slice pieces
                                    float sliceAngle = atan2(mouse_y - prev_mouse_y, mouse_x - prev_mouse_x);

                                    // Create two pieces moving in different directions
                                    for (int j = 0; j < SLICE_PIECES; j++)
                                    {
                                        gameObjects[i].pieces[j].x = center_x;
                                        gameObjects[i].pieces[j].y = center_y;

                                        // Different velocities for each piece
                                        float pieceAngle = sliceAngle + (j == 0 ? M_PI / 2 : -M_PI / 2);
                                        float speed = (2.0f + (rand() % 20) / 10.0f) * 1.5f; // 50% faster

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
                                        // Reduce health when bomb is sliced
                                        health--;
                                        if (health <= 0)
                                        {
                                            printf("Game Over! Final score: %d\n", score);
                                            health = 0; // Ensure health doesn't go below 0
                                            game_state = STATE_GAME_OVER;
                                            addScore(score);
                                        }
                                        // No score penalty for bombs
                                        printf("Bomb sliced! Health: %d\n", health);
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
                    }

                    // Also check slice along multiple points on the path for very precise slicing
                    int samples = 12; // Good balance of precision and performance

                    for (int t = 0; t <= samples; t++)
                    {
                        float lerp = (float)t / samples;
                        int slice_x = prev_mouse_x + (mouse_x - prev_mouse_x) * lerp;
                        int slice_y = prev_mouse_y + (mouse_y - prev_mouse_y) * lerp;

                        for (int i = 0; i < MAX_FRUITS; i++)
                        {
                            // Only process objects that haven't been sliced yet in this motion
                            if (gameObjects[i].active && !gameObjects[i].sliced && !sliced_objects[i])
                            {
                                // Use improved collision detection function
                                if (checkCollision(slice_x, slice_y, &gameObjects[i]))
                                {
                                    gameObjects[i].sliced = 1;
                                    sliced_objects[i] = 1;

                                    // Initialize slice pieces
                                    float sliceAngle = atan2(mouse_y - prev_mouse_y, mouse_x - prev_mouse_x);
                                    float center_x = gameObjects[i].x + FRUIT_SIZE / 2;
                                    float center_y = gameObjects[i].y + FRUIT_SIZE / 2;

                                    // Create two pieces moving in different directions
                                    for (int j = 0; j < SLICE_PIECES; j++)
                                    {
                                        gameObjects[i].pieces[j].x = center_x;
                                        gameObjects[i].pieces[j].y = center_y;

                                        // Different velocities for each piece
                                        float pieceAngle = sliceAngle + (j == 0 ? M_PI / 2 : -M_PI / 2);
                                        float speed = (2.0f + (rand() % 20) / 10.0f) * 1.5f; // 50% faster

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
                                        // Reduce health when bomb is sliced
                                        health--;
                                        if (health <= 0)
                                        {
                                            printf("Game Over! Final score: %d\n", score);
                                            health = 0; // Ensure health doesn't go below 0
                                            game_state = STATE_GAME_OVER;
                                            addScore(score);
                                        }
                                        // No score penalty for bombs
                                        printf("Bomb sliced! Health: %d\n", health);
                                    }
                                    else
                                    {
                                        // Play slice sound
                                        Mix_PlayChannel(-1, sliceSound, 0);
                                        score += 1;
                                        printf("Fruit sliced! Score: %d\n", score);
                                    }

                                    // Don't break here - need to check remaining fruits
                                }
                            }
                        }
                    }

                    pthread_mutex_unlock(&game_mutex);

                    // Set mouse_down to true for rendering the slice trail
                    mouse_down = 1;
                }
                else
                {
                    // If mouse barely moved, don't show the slice trail
                    mouse_down = 0;
                }
            }
        }
        else if (e.type == SDL_MOUSEBUTTONDOWN)
        {
            // Game over screen button clicks
            if (game_state == STATE_GAME_OVER)
            {
                int mouseX = e.button.x;
                int mouseY = e.button.y;

                // Check if the click is on the "Restart" button
                SDL_Rect restartButton = {WINDOW_WIDTH / 2 - 120, WINDOW_HEIGHT / 2 + 20, 100, 40};
                if (mouseX >= restartButton.x && mouseX <= restartButton.x + restartButton.w &&
                    mouseY >= restartButton.y && mouseY <= restartButton.y + restartButton.h)
                {
                    // Reset the game
                    resetGame();
                    return;
                }

                // Check if the click is on the "Leaderboard" button
                SDL_Rect leaderboardButton = {WINDOW_WIDTH / 2 + 20, WINDOW_HEIGHT / 2 + 20, 100, 40};
                if (mouseX >= leaderboardButton.x && mouseX <= leaderboardButton.x + leaderboardButton.w &&
                    mouseY >= leaderboardButton.y && mouseY <= leaderboardButton.y + leaderboardButton.h)
                {
                    // Show leaderboard
                    game_state = STATE_LEADERBOARD;
                    return;
                }
            }
            // Leaderboard screen back button click
            else if (game_state == STATE_LEADERBOARD)
            {
                int mouseX = e.button.x;
                int mouseY = e.button.y;

                // Check if the click is on the "Back" button
                SDL_Rect backButton = {WINDOW_WIDTH / 2 - 50, WINDOW_HEIGHT - 70, 100, 40};
                if (mouseX >= backButton.x && mouseX <= backButton.x + backButton.w &&
                    mouseY >= backButton.y && mouseY <= backButton.y + backButton.h)
                {
                    // Return to game over screen
                    game_state = STATE_GAME_OVER;
                    return;
                }
            }
        }
        else if (e.type == SDL_KEYDOWN)
        {
            if (e.key.keysym.sym == SDLK_ESCAPE)
            {
                // In leaderboard state, go back to game over screen
                if (game_state == STATE_LEADERBOARD)
                {
                    game_state = STATE_GAME_OVER;
                }
                else
                {
                    running = 0;
                }
            }
            else if (e.key.keysym.sym == SDLK_r && game_state != STATE_PLAYING)
            {
                // Reset game if not currently playing
                resetGame();
            }
        }
    }
}

// Update game state
void updateGame()
{
    pthread_mutex_lock(&game_mutex);

    // Only update game objects if the game is active
    if (game_state == STATE_PLAYING)
    {
        // Update game timer
        Uint32 current_time = SDL_GetTicks();
        game_time = (current_time - start_time) / 1000; // Convert to seconds

        // Check if game is over due to no health
        if (health <= 0)
        {
            // Change game state and save score
            game_state = STATE_GAME_OVER;
            addScore(score);
        }

        for (int i = 0; i < MAX_FRUITS; i++)
        {
            if (gameObjects[i].active)
            {
                // Update main fruit position
                gameObjects[i].vy += 0.3f; // Increased gravity effect (was 0.2f)
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
                            gameObjects[i].pieces[j].vy += 0.45f; // Heavier gravity for pieces (was 0.3f)
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

                        // No penalty for missing a fruit - REMOVED
                        // Just deactivate the fruit without affecting score
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

    // ===== Draw Score Panel =====
    // Create a nice-looking score panel in top-left
    for (int i = 0; i < 40; i++)
    {
        int alpha = 180 - i * 3;
        if (alpha < 0)
            alpha = 0;
        SDL_SetRenderDrawColor(renderer, 30, 30, 60, alpha);
        SDL_Rect scoreGradient = {10, 10 + i, 120, 1};
        SDL_RenderFillRect(renderer, &scoreGradient);
    }

    // Main score box
    SDL_SetRenderDrawColor(renderer, 30, 30, 60, 180);
    SDL_Rect scoreRect = {10, 10, 140, 40}; // Increased width from 120 to 140
    SDL_RenderFillRect(renderer, &scoreRect);

    // Border for score box
    SDL_SetRenderDrawColor(renderer, 200, 150, 100, 255); // Changed color from blue to amber
    SDL_Rect scoreBorder = {10, 10, 140, 40};             // Increased width from 120 to 140
    SDL_RenderDrawRect(renderer, &scoreBorder);

    // Draw score text
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

    // Display score number (simplistic digital-style)
    char scoreStr[20];
    sprintf(scoreStr, "%d", score);
    int digitWidth = 10; // Reduced from 12 to 10

    // Calculate position to center score in its box
    int scoreDigits = strlen(scoreStr);
    int totalScoreWidth = scoreDigits * digitWidth + (scoreDigits - 1) * 2; // Width plus spacing
    int digitX = scoreRect.x + (scoreRect.w - totalScoreWidth) / 2;
    int digitY = scoreRect.y + (scoreRect.h - 18) / 2; // Center vertically (18 is digit height)

    // Set drawing color back to white for the score display
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

    // Display score as a digital-style number
    for (int i = 0; scoreStr[i] != '\0'; i++)
    {
        int digit = scoreStr[i] - '0';

        // Simple digital rendering - rectangles for each segment
        // Adjust positioning based on digit
        SDL_Rect segments[7]; // 7 segments in a digital display

        // Initialize segments to off position
        for (int s = 0; s < 7; s++)
        {
            segments[s].x = digitX + i * digitWidth;
            segments[s].y = digitY;
            segments[s].w = 8;
            segments[s].h = 2;
        }

        // Horizontal segments
        segments[0].y = digitY;      // Top
        segments[3].y = digitY + 8;  // Middle
        segments[6].y = digitY + 16; // Bottom

        // Vertical segments
        segments[1].x = digitX + i * digitWidth + 8; // Top right
        segments[1].y = digitY;
        segments[1].w = 2;
        segments[1].h = 8;

        segments[2].x = digitX + i * digitWidth + 8; // Bottom right
        segments[2].y = digitY + 8;
        segments[2].w = 2;
        segments[2].h = 8;

        segments[4].x = digitX + i * digitWidth; // Top left
        segments[4].y = digitY;
        segments[4].w = 2;
        segments[4].h = 8;

        segments[5].x = digitX + i * digitWidth; // Bottom left
        segments[5].y = digitY + 8;
        segments[5].w = 2;
        segments[5].h = 8;

        // Define which segments are on for each digit
        bool segmentOn[10][7] = {
            {1, 1, 1, 0, 1, 1, 1}, // 0
            {0, 1, 1, 0, 0, 0, 0}, // 1
            {1, 1, 0, 1, 0, 1, 1}, // 2
            {1, 1, 1, 1, 0, 0, 1}, // 3
            {0, 1, 1, 1, 1, 0, 0}, // 4
            {1, 0, 1, 1, 1, 0, 1}, // 5
            {1, 0, 1, 1, 1, 1, 1}, // 6
            {1, 1, 1, 0, 0, 0, 0}, // 7
            {1, 1, 1, 1, 1, 1, 1}, // 8
            {1, 1, 1, 1, 1, 0, 1}  // 9
        };

        // Render the active segments for this digit
        for (int s = 0; s < 7; s++)
        {
            if (segmentOn[digit][s])
            {
                SDL_RenderFillRect(renderer, &segments[s]);
            }
        }
    }

    // ===== Draw Timer in top-middle =====
    int minutes = game_time / 60;
    int seconds = game_time % 60;

    // Timer background
    SDL_SetRenderDrawColor(renderer, 30, 30, 60, 180);
    SDL_Rect timerRect = {WINDOW_WIDTH / 2 - 60, 10, 120, 50}; // Increased width from 100 to 120
    SDL_RenderFillRect(renderer, &timerRect);

    // Timer border
    SDL_SetRenderDrawColor(renderer, 100, 200, 100, 255);
    SDL_Rect timerBorder = {WINDOW_WIDTH / 2 - 60, 10, 120, 50}; // Increased width from 100 to 120
    SDL_RenderDrawRect(renderer, &timerBorder);

    // Display timer as MM:SS
    char timeStr[10];
    sprintf(timeStr, "%02d:%02d", minutes, seconds);

    // Improved timer rendering with better digital display
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

    // Calculate position to center timer text
    int timerDigitWidth = 10; // Increased for better visibility
    int digitHeight = 24;     // Increased for better visibility
    int segmentThickness = 3; // Thicker segments

    // Calculate width of timer text (5 characters plus spacing)
    int timerTextWidth = 5 * timerDigitWidth + 4 * 2; // 5 chars (MM:SS) with 2px spacing between each
    int timeX = WINDOW_WIDTH / 2 - timerTextWidth / 2;
    int timeY = timerRect.y + (timerRect.h - digitHeight) / 2; // Center vertically

    // Draw each character in the time string with improved digital segments
    for (int i = 0; timeStr[i] != '\0'; i++)
    {
        if (timeStr[i] == ':')
        {
            // Draw colon with larger dots
            int dotSize = digitHeight / 5;
            int dotY1 = timeY + digitHeight / 3 - dotSize / 2;
            int dotY2 = timeY + 2 * digitHeight / 3 - dotSize / 2;
            SDL_Rect colon1 = {timeX + i * (timerDigitWidth + 2), dotY1, dotSize, dotSize};
            SDL_Rect colon2 = {timeX + i * (timerDigitWidth + 2), dotY2, dotSize, dotSize};
            SDL_RenderFillRect(renderer, &colon1);
            SDL_RenderFillRect(renderer, &colon2);
        }
        else
        {
            // Convert character to digit
            int digit = timeStr[i] - '0';
            int x = timeX + i * (timerDigitWidth + 2);
            int y = timeY;

            // Arrays to define which segments are on for each digit (7-segment display)
            // Segments: 0=top, 1=top-right, 2=bottom-right, 3=bottom, 4=bottom-left, 5=top-left, 6=middle
            bool segments[10][7] = {
                {1, 1, 1, 1, 1, 1, 0}, // 0
                {0, 1, 1, 0, 0, 0, 0}, // 1
                {1, 1, 0, 1, 1, 0, 1}, // 2
                {1, 1, 1, 1, 0, 0, 1}, // 3
                {0, 1, 1, 0, 0, 1, 1}, // 4
                {1, 0, 1, 1, 0, 1, 1}, // 5
                {1, 0, 1, 1, 1, 1, 1}, // 6
                {1, 1, 1, 0, 0, 0, 0}, // 7
                {1, 1, 1, 1, 1, 1, 1}, // 8
                {1, 1, 1, 1, 0, 1, 1}  // 9
            };

            // Define coordinates for each segment
            SDL_Rect segs[7];

            // Horizontal segments (top, middle, bottom)
            segs[0] = (SDL_Rect){x, y, timerDigitWidth, segmentThickness}; // Top

            // Define local variables needed for rendering
            int digitHeight = 30;                // Height of the digit
            int digitWidth = timerDigitWidth;    // Use existing width
            int segThickness = segmentThickness; // Use existing thickness

            segs[6] = (SDL_Rect){x, y + digitHeight / 2 - segThickness / 2, digitWidth, segThickness}; // Middle
            segs[3] = (SDL_Rect){x, y + digitHeight - segThickness, digitWidth, segThickness};         // Bottom

            // Vertical segments (top-right, bottom-right, bottom-left, top-left)
            segs[1] = (SDL_Rect){x + digitWidth - segThickness, y, segThickness, digitHeight / 2};                   // Top-right
            segs[2] = (SDL_Rect){x + digitWidth - segThickness, y + digitHeight / 2, segThickness, digitHeight / 2}; // Bottom-right
            segs[4] = (SDL_Rect){x, y + digitHeight / 2, segThickness, digitHeight / 2};                             // Bottom-left
            segs[5] = (SDL_Rect){x, y, segThickness, digitHeight / 2};                                               // Top-left

            // Draw the active segments for this digit
            for (int s = 0; s < 7; s++)
            {
                if (segments[digit][s])
                {
                    SDL_RenderFillRect(renderer, &segs[s]);
                }
            }
        }
    }

    // ===== Draw Health Hearts in top-right =====
    // Health background
    SDL_SetRenderDrawColor(renderer, 30, 30, 60, 180);
    SDL_Rect healthRect = {WINDOW_WIDTH - 130, 10, 120, 50}; // Increased height to match timer
    SDL_RenderFillRect(renderer, &healthRect);

    // Health border
    SDL_SetRenderDrawColor(renderer, 200, 100, 100, 255);
    SDL_Rect healthBorder = {WINDOW_WIDTH - 130, 10, 120, 50}; // Increased height to match timer
    SDL_RenderDrawRect(renderer, &healthBorder);

    // Draw hearts
    // Calculate total width of all hearts with spacing between them
    int heartWidth = 16;   // Approximate width of a heart
    int heartSpacing = 25; // Space between hearts
    int totalHeartsWidth = (3 * heartWidth) + (2 * heartSpacing);
    int startX = healthRect.x + (healthRect.w - totalHeartsWidth) / 2;

    for (int i = 0; i < 3; i++)
    {
        if (i < health)
        {
            // Full heart - red
            SDL_SetRenderDrawColor(renderer, 255, 50, 50, 255);
        }
        else
        {
            // Empty heart - gray outline
            SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
        }

        // Draw a heart shape - simplified
        int heartX = startX + i * (heartWidth + heartSpacing);
        int heartY = healthRect.y + healthRect.h / 2; // Center hearts vertically in the box

        // Draw heart shape (very simplified)
        SDL_Point heartPoints[] = {
            {heartX, heartY + 5},
            {heartX - 8, heartY - 3},
            {heartX - 4, heartY - 7},
            {heartX, heartY - 2},
            {heartX + 4, heartY - 7},
            {heartX + 8, heartY - 3},
            {heartX, heartY + 5}};

        SDL_RenderDrawLines(renderer, heartPoints, 7);

        // Fill heart if it's a full heart
        if (i < health)
        {
            for (int y = heartY - 6; y <= heartY + 4; y++)
            {
                for (int x = heartX - 7; x <= heartX + 7; x++)
                {
                    // Simple formula to check if point is inside heart shape
                    int dx = x - heartX;
                    int dy = y - heartY;
                    if ((dx * dx + dy * dy) < 50 && y <= heartY + 5)
                    {
                        SDL_RenderDrawPoint(renderer, x, y);
                    }
                }
            }
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
        static float trailOpacity[15] = {0}; // Increased from 10 to 15 for longer trail
        static int trailX[15] = {0};         // X positions for trail segments
        static int trailY[15] = {0};         // Y positions for trail segments
        static float trailWidth[15] = {0};   // Width of each trail segment

        // Shift trail values
        for (int i = 14; i > 0; i--)
        {
            trailX[i] = trailX[i - 1];
            trailY[i] = trailY[i - 1];
            trailOpacity[i] = trailOpacity[i - 1] * 0.85f; // Slower fade out (was 0.8f)
            trailWidth[i] = trailWidth[i - 1] * 0.9f;      // Gradual thinning
        }

        // Add new point to trail
        trailX[0] = mouse_x;
        trailY[0] = mouse_y;
        trailOpacity[0] = 1.0f; // Full opacity at start (was 0.9f)

        // Calculate trail width based on mouse movement speed
        float movement = sqrt(pow(mouse_x - prev_mouse_x, 2) + pow(mouse_y - prev_mouse_y, 2));
        trailWidth[0] = fmin(3.5f, 1.5f + movement * 0.05f); // Thinner trail: was 6.0f max, now 3.5f max

        // Draw trail with improved gradient
        for (int i = 1; i < 15; i++)
        {
            if (trailOpacity[i] > 0.05f)
            {
                int alpha = (int)(trailOpacity[i] * 255);
                float thickness = trailWidth[i];
                if (thickness < 0.5f)
                    thickness = 0.5f;

                // Bright core
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, alpha);
                SDL_RenderDrawLine(renderer, trailX[i - 1], trailY[i - 1], trailX[i], trailY[i]);

                // Thinner colored trail with better rainbow effect
                for (int t = 1; t <= (int)(thickness * 1.5); t++) // Reduced multiplier from 2 to 1.5
                {
                    float tFactor = t / thickness;

                    // Vibrant sword-like colors
                    Uint8 r, g, b;
                    // Calculate hue based on position in trail and current time for animation
                    float hue = (i * 20 + SDL_GetTicks() / 10) % 360;

                    // Simple HSV to RGB conversion for vibrant colors
                    if (hue < 60)
                    {
                        r = 255;
                        g = (Uint8)(hue * 4.25);
                        b = 0;
                    }
                    else if (hue < 120)
                    {
                        r = (Uint8)((120 - hue) * 4.25);
                        g = 255;
                        b = 0;
                    }
                    else if (hue < 180)
                    {
                        r = 0;
                        g = 255;
                        b = (Uint8)((hue - 120) * 4.25);
                    }
                    else if (hue < 240)
                    {
                        r = 0;
                        g = (Uint8)((240 - hue) * 4.25);
                        b = 255;
                    }
                    else if (hue < 300)
                    {
                        r = (Uint8)((hue - 240) * 4.25);
                        g = 0;
                        b = 255;
                    }
                    else
                    {
                        r = 255;
                        g = 0;
                        b = (Uint8)((360 - hue) * 4.25);
                    }

                    // Adjust alpha based on distance from center of trail
                    int edgeAlpha = (int)(alpha / (tFactor + 1));

                    SDL_SetRenderDrawColor(renderer, r, g, b, edgeAlpha);

                    // Draw parallel lines to create thickness
                    float angle = atan2(trailY[i] - trailY[i - 1], trailX[i] - trailX[i - 1]) + M_PI / 2;
                    float distance = t * 0.5f; // Reduced from 0.7f to 0.5f for thinner trail

                    int offsetX = (int)(cos(angle) * distance);
                    int offsetY = (int)(sin(angle) * distance);

                    SDL_RenderDrawLine(renderer,
                                       trailX[i - 1] + offsetX, trailY[i - 1] + offsetY,
                                       trailX[i] + offsetX, trailY[i] + offsetY);

                    SDL_RenderDrawLine(renderer,
                                       trailX[i - 1] - offsetX, trailY[i - 1] - offsetY,
                                       trailX[i] - offsetX, trailY[i] - offsetY);
                }

                // Add smaller sparkle effects
                if (i % 3 == 0) // Less frequent sparkles (was i % 2)
                {
                    int sparkleSize = 2 - i / 7; // Smaller sparkles (was 4 - i/5)
                    if (sparkleSize > 0)
                    {
                        // Brighter sparkle color
                        SDL_SetRenderDrawColor(renderer, 255, 255, 220, alpha);

                        // Draw as a filled circle instead of a rectangle for better appearance
                        filledCircleRGBA(renderer,
                                         trailX[i],
                                         trailY[i],
                                         sparkleSize,
                                         255, 255, 220, alpha);
                    }
                }
            }
        }
    }

    // If game over, display a message
    if (game_state == STATE_GAME_OVER)
    {
        // Semi-transparent overlay
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
        SDL_Rect overlay = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
        SDL_RenderFillRect(renderer, &overlay);

        // Game over message box
        SDL_SetRenderDrawColor(renderer, 50, 50, 70, 240);
        SDL_Rect messageBox = {WINDOW_WIDTH / 2 - 150, WINDOW_HEIGHT / 2 - 100, 300, 200};
        SDL_RenderFillRect(renderer, &messageBox);

        // Box border
        SDL_SetRenderDrawColor(renderer, 200, 50, 50, 255);
        SDL_Rect messageBorder = {WINDOW_WIDTH / 2 - 150, WINDOW_HEIGHT / 2 - 100, 300, 200};
        SDL_RenderDrawRect(renderer, &messageBorder);

        // Game Over Text with digital display
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

        // Draw "GAME OVER" text centered
        int gameOverWidth = strlen("GAME OVER") * 18 + (strlen("GAME OVER") - 1) * 3;
        int gameOverX = WINDOW_WIDTH / 2 - gameOverWidth / 2;
        drawDigitalText(renderer, "GAME OVER", gameOverX, WINDOW_HEIGHT / 2 - 70, 18, 28, 3);

        // Draw final score with digital display centered
        char scoreStr[20];
        sprintf(scoreStr, "SCORE %d", score);
        int scoreTextWidth = strlen(scoreStr) * 14 + (strlen(scoreStr) - 1) * 3;
        int scoreTextX = WINDOW_WIDTH / 2 - scoreTextWidth / 2;
        drawDigitalText(renderer, scoreStr, scoreTextX, WINDOW_HEIGHT / 2 - 20, 14, 22, 3);

        // Render "Restart" button
        SDL_SetRenderDrawColor(renderer, 80, 100, 200, 255);
        SDL_Rect restartButton = {WINDOW_WIDTH / 2 - 120, WINDOW_HEIGHT / 2 + 20, 100, 40};
        SDL_RenderFillRect(renderer, &restartButton);

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderDrawRect(renderer, &restartButton);

        // Draw "RESTART" text centered in button
        int restartWidth = strlen("RESTART") * 10 + (strlen("RESTART") - 1) * 2;
        int restartX = restartButton.x + (restartButton.w - restartWidth) / 2;
        int restartY = restartButton.y + (restartButton.h - 20) / 2;
        drawDigitalText(renderer, "RESTART", restartX, restartY, 10, 20, 2);

        // Render "Leaderboard" button
        SDL_SetRenderDrawColor(renderer, 80, 100, 200, 255);
        SDL_Rect leaderboardButton = {WINDOW_WIDTH / 2 + 20, WINDOW_HEIGHT / 2 + 20, 100, 40};
        SDL_RenderFillRect(renderer, &leaderboardButton);

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderDrawRect(renderer, &leaderboardButton);

        // Draw "SCORES" text centered in button
        int scoresWidth = strlen("SCORES") * 10 + (strlen("SCORES") - 1) * 2;
        int scoresX = leaderboardButton.x + (leaderboardButton.w - scoresWidth) / 2;
        int scoresY = leaderboardButton.y + (leaderboardButton.h - 20) / 2;
        drawDigitalText(renderer, "SCORES", scoresX, scoresY, 10, 20, 2);
    }
    // Display leaderboard
    else if (game_state == STATE_LEADERBOARD)
    {
        // Semi-transparent overlay
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
        SDL_Rect overlay = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
        SDL_RenderFillRect(renderer, &overlay);

        // Leaderboard box
        SDL_SetRenderDrawColor(renderer, 50, 50, 70, 240);
        SDL_Rect leaderboardBox = {WINDOW_WIDTH / 2 - 220, 50, 440, WINDOW_HEIGHT - 150}; // Increased width from 400 to 440
        SDL_RenderFillRect(renderer, &leaderboardBox);

        // Box border
        SDL_SetRenderDrawColor(renderer, 100, 100, 200, 255);
        SDL_RenderDrawRect(renderer, &leaderboardBox);

        // Draw "LEADERBOARD" title with digital display
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

        // Center the "LEADERBOARD" title
        int titleWidth = strlen("LEADERBOARD") * 16 + (strlen("LEADERBOARD") - 1) * 3; // Character width * count + spacing
        int titleX = WINDOW_WIDTH / 2 - titleWidth / 2;
        drawDigitalText(renderer, "LEADERBOARD", titleX, 70, 16, 28, 3); // Centered

        // Display each score entry with digital display
        int maxDisplayScores = 9; // Changed from 10 to 9
        for (int i = 0; i < num_scores && i < maxDisplayScores; i++)
        {
            int y = 130 + i * 40;
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

            // Calculate the width of a row to center contents
            char fullRowText[50];
            sprintf(fullRowText, "%d.  %d  %s", i + 1, leaderboard[i].score, leaderboard[i].date);

            // Rank - left aligned with padding from left edge
            char rankStr[5];
            sprintf(rankStr, "%d.", i + 1);
            drawDigitalText(renderer, rankStr, leaderboardBox.x + 30, y, 12, 22, 3);

            // Score - centered in the middle section
            char scoreStr[20];
            sprintf(scoreStr, "%d", leaderboard[i].score);
            drawDigitalText(renderer, scoreStr, WINDOW_WIDTH / 2 - (strlen(scoreStr) * 12) / 2, y, 12, 22, 3);

            // Date (shortened to just show essential info) - right aligned with padding
            char dateShort[15];
            strncpy(dateShort, leaderboard[i].date, 10); // Just show the date part
            dateShort[10] = '\0';
            drawDigitalText(renderer, dateShort, leaderboardBox.x + leaderboardBox.w - 30 - (strlen(dateShort) * 8), y, 8, 18, 2);
        }

        // Back button
        SDL_SetRenderDrawColor(renderer, 80, 100, 200, 255);
        SDL_Rect backButton = {WINDOW_WIDTH / 2 - 50, WINDOW_HEIGHT - 70, 100, 40};
        SDL_RenderFillRect(renderer, &backButton);

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderDrawRect(renderer, &backButton);

        // Draw "BACK" text centered in button
        int backWidth = strlen("BACK") * 12 + (strlen("BACK") - 1) * 2;
        int backX = backButton.x + (backButton.w - backWidth) / 2;
        int backY = backButton.y + (backButton.h - 20) / 2;
        drawDigitalText(renderer, "BACK", backX, backY, 12, 20, 2);
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

    // Cancel deadlock thread
    pthread_cancel(deadlock_thread);
    pthread_join(deadlock_thread, NULL);

    // Clean up deadlock detector resources
    cleanupDeadlockDetector();

    printf("Game cleaned up successfully\n");
}

// Save high score to file
void saveScore()
{
    // Add the current score to the leaderboard
    addScore(score);
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

// Function to reset the game
void resetGame()
{
    // Reset score and health
    score = 0;
    health = 3;
    game_time = 0;
    start_time = SDL_GetTicks();
    game_state = STATE_PLAYING;

    // Clear any existing game objects
    for (int i = 0; i < MAX_FRUITS; i++)
    {
        gameObjects[i].active = 0;
    }

    printf("Game reset! Ready to play again.\n");
}

// Load scores from file
void loadScores()
{
    FILE *file = fopen("leaderboard.txt", "r");
    if (file == NULL)
    {
        printf("No leaderboard file found. Starting fresh.\n");
        return;
    }

    num_scores = 0;
    while (num_scores < MAX_SCORES &&
           fscanf(file, "%d,%19[^\n]\n", &leaderboard[num_scores].score, leaderboard[num_scores].date) == 2)
    {
        num_scores++;
    }

    fclose(file);
    printf("Loaded %d scores from leaderboard file.\n", num_scores);
}

// Save scores to file
void saveScores()
{
    FILE *file = fopen("leaderboard.txt", "w");
    if (file == NULL)
    {
        perror("Failed to open leaderboard file for writing");
        return;
    }

    for (int i = 0; i < num_scores; i++)
    {
        fprintf(file, "%d,%s\n", leaderboard[i].score, leaderboard[i].date);
    }

    fclose(file);
    printf("Saved %d scores to leaderboard file.\n", num_scores);
}

// Add a score to the leaderboard
void addScore(int new_score)
{
    // Get current date and time
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char date_str[20];
    strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M:%S", t);

    // Check if the score qualifies for the leaderboard
    if (num_scores < MAX_SCORES || new_score > leaderboard[num_scores - 1].score)
    {
        // Find the position to insert the new score
        int pos = 0;
        while (pos < num_scores && new_score <= leaderboard[pos].score)
        {
            pos++;
        }

        // Shift lower scores down
        if (num_scores < MAX_SCORES)
        {
            num_scores++;
        }

        for (int i = num_scores - 1; i > pos; i--)
        {
            leaderboard[i] = leaderboard[i - 1];
        }

        // Insert the new score
        leaderboard[pos].score = new_score;
        strcpy(leaderboard[pos].date, date_str);

        // Save the updated leaderboard
        saveScores();
        printf("Added score %d to leaderboard at position %d\n", new_score, pos + 1);
    }
    else
    {
        printf("Score %d did not make the leaderboard.\n", new_score);
    }
}

// Helper function to draw a digital character
void drawDigitalChar(SDL_Renderer *renderer, char c, int x, int y, int w, int h)
{
    // Segment thickness (adjust based on character size)
    int thickness = h / 8;
    if (thickness < 1)
        thickness = 1;

    // Define segments for digits and letters
    bool segments[7] = {0, 0, 0, 0, 0, 0, 0}; // Default all off

    // Define segments for each character
    // Segments: 0=top, 1=top-right, 2=bottom-right, 3=bottom, 4=bottom-left, 5=top-left, 6=middle
    if (c >= '0' && c <= '9')
    {
        // Digits
        switch (c)
        {
        case '0':
            segments[0] = segments[1] = segments[2] = segments[3] = segments[4] = segments[5] = 1;
            break;
        case '1':
            segments[1] = segments[2] = 1;
            break;
        case '2':
            segments[0] = segments[1] = segments[3] = segments[4] = segments[6] = 1;
            break;
        case '3':
            segments[0] = segments[1] = segments[2] = segments[3] = segments[6] = 1;
            break;
        case '4':
            segments[1] = segments[2] = segments[5] = segments[6] = 1;
            break;
        case '5':
            segments[0] = segments[2] = segments[3] = segments[5] = segments[6] = 1;
            break;
        case '6':
            segments[0] = segments[2] = segments[3] = segments[4] = segments[5] = segments[6] = 1;
            break;
        case '7':
            segments[0] = segments[1] = segments[2] = 1;
            break;
        case '8':
            segments[0] = segments[1] = segments[2] = segments[3] = segments[4] = segments[5] = segments[6] = 1;
            break;
        case '9':
            segments[0] = segments[1] = segments[2] = segments[3] = segments[5] = segments[6] = 1;
            break;
        }
    }
    else
    {
        // Letters
        switch (c)
        {
        case 'A':
            segments[0] = segments[1] = segments[2] = segments[4] = segments[5] = segments[6] = 1;
            break;
        case 'B':
            segments[2] = segments[3] = segments[4] = segments[5] = segments[6] = 1;
            break;
        case 'C':
            segments[0] = segments[3] = segments[4] = segments[5] = 1;
            break;
        case 'D':
            segments[1] = segments[2] = segments[3] = segments[4] = segments[6] = 1;
            break;
        case 'E':
            segments[0] = segments[3] = segments[4] = segments[5] = segments[6] = 1;
            break;
        case 'F':
            segments[0] = segments[4] = segments[5] = segments[6] = 1;
            break;
        case 'G':
            segments[0] = segments[2] = segments[3] = segments[4] = segments[5] = segments[6] = 1;
            break;
        case 'H':
            segments[1] = segments[2] = segments[4] = segments[5] = segments[6] = 1;
            break;
        case 'I':
            segments[1] = segments[2] = 1;
            break;
        case 'J':
            segments[1] = segments[2] = segments[3] = segments[4] = 1;
            break;
        case 'K':
            segments[1] = segments[2] = segments[4] = segments[5] = segments[6] = 1;
            break;
        case 'L':
            segments[3] = segments[4] = segments[5] = 1;
            break;
        case 'M':
            segments[0] = segments[1] = segments[2] = segments[4] = segments[5] = 1;
            break;
        case 'N':
            segments[0] = segments[1] = segments[2] = segments[4] = segments[5] = 1;
            break;
        case 'O':
            segments[0] = segments[1] = segments[2] = segments[3] = segments[4] = segments[5] = 1;
            break;
        case 'P':
            segments[0] = segments[1] = segments[4] = segments[5] = segments[6] = 1;
            break;
        case 'Q':
            segments[0] = segments[1] = segments[2] = segments[3] = segments[5] = segments[6] = 1;
            break;
        case 'R':
            segments[0] = segments[1] = segments[4] = segments[5] = 1;
            break;
        case 'S':
            segments[0] = segments[2] = segments[3] = segments[5] = segments[6] = 1;
            break;
        case 'T':
            segments[0] = segments[4] = segments[5] = segments[6] = 1;
            break;
        case 'U':
            segments[1] = segments[2] = segments[3] = segments[4] = segments[5] = 1;
            break;
        case 'V':
            segments[1] = segments[2] = segments[3] = segments[4] = segments[5] = 1;
            break;
        case 'W':
            segments[1] = segments[2] = segments[3] = segments[4] = segments[5] = 1;
            break;
        case 'X':
            segments[1] = segments[2] = segments[4] = segments[5] = segments[6] = 1;
            break;
        case 'Y':
            segments[1] = segments[2] = segments[3] = segments[5] = segments[6] = 1;
            break;
        case 'Z':
            segments[0] = segments[1] = segments[3] = segments[4] = segments[6] = 1;
            break;
        case ':':
        {
            // Draw colon (special case)
            int dotSize = h / 5;
            SDL_Rect dot1 = {x + w / 2 - dotSize / 2, y + h / 3 - dotSize / 2, dotSize, dotSize};
            SDL_Rect dot2 = {x + w / 2 - dotSize / 2, y + 2 * h / 3 - dotSize / 2, dotSize, dotSize};
            SDL_RenderFillRect(renderer, &dot1);
            SDL_RenderFillRect(renderer, &dot2);
            return;
        }
        case '.':
        {
            // Draw period (special case)
            SDL_Rect dot = {x + w / 2 - thickness / 2, y + h - thickness, thickness, thickness};
            SDL_RenderFillRect(renderer, &dot);
            return;
        }
        case '-':
            // Draw hyphen (just the middle segment)
            segments[6] = 1;
            break;
        case '_':
            // Draw underscore (just the bottom segment)
            segments[3] = 1;
            break;
        case ' ':
            // Space - all segments off
            return;
        default:
        {
            // Default unknown character to a filled rectangle
            SDL_Rect unknown = {x + w / 4, y + h / 3, w / 2, h / 3};
            SDL_RenderFillRect(renderer, &unknown);
            return;
        }
        }
    }

    // Define coordinates for each segment
    SDL_Rect segs[7];

    // Horizontal segments
    segs[0].x = x;
    segs[0].y = y;
    segs[0].w = w;
    segs[0].h = thickness;

    segs[6].x = x;
    segs[6].y = y + h / 2 - thickness / 2;
    segs[6].w = w;
    segs[6].h = thickness;

    segs[3].x = x;
    segs[3].y = y + h - thickness;
    segs[3].w = w;
    segs[3].h = thickness;

    // Vertical segments
    segs[1].x = x + w - thickness;
    segs[1].y = y;
    segs[1].w = thickness;
    segs[1].h = h / 2;

    segs[2].x = x + w - thickness;
    segs[2].y = y + h / 2;
    segs[2].w = thickness;
    segs[2].h = h / 2;

    segs[4].x = x;
    segs[4].y = y + h / 2;
    segs[4].w = thickness;
    segs[4].h = h / 2;

    segs[5].x = x;
    segs[5].y = y;
    segs[5].w = thickness;
    segs[5].h = h / 2;

    // Draw the active segments
    for (int s = 0; s < 7; s++)
    {
        if (segments[s])
        {
            SDL_RenderFillRect(renderer, &segs[s]);
        }
    }
}

// Helper function to draw text using the digital display method
void drawDigitalText(SDL_Renderer *renderer, const char *text, int x, int y, int charWidth, int charHeight, int spacing)
{
    int len = strlen(text);
    for (int i = 0; i < len; i++)
    {
        // Convert lowercase to uppercase for consistent rendering
        char c = text[i];
        if (c >= 'a' && c <= 'z')
        {
            c = c - 'a' + 'A';
        }

        drawDigitalChar(renderer, c, x + i * (charWidth + spacing), y, charWidth, charHeight);
    }
}

// Function to draw a string using our digital character rendering
void drawString(SDL_Renderer *renderer, const char *str, int x, int y, int charWidth, int charHeight, int spacing)
{
    if (!str)
        return;

    int currentX = x;

    for (int i = 0; str[i] != '\0'; i++)
    {
        drawDigitalChar(renderer, str[i], currentX, y, charWidth, charHeight);
        currentX += charWidth + spacing;
    }
}

// Function to draw centered text
void drawCenteredString(SDL_Renderer *renderer, const char *str, int centerX, int y, int charWidth, int charHeight, int spacing)
{
    if (!str)
        return;

    int textWidth = (strlen(str) * charWidth) + ((strlen(str) - 1) * spacing);
    int startX = centerX - (textWidth / 2);

    drawString(renderer, str, startX, y, charWidth, charHeight, spacing);
}

// Function to draw right-aligned text
void drawRightAlignedString(SDL_Renderer *renderer, const char *str, int rightX, int y, int charWidth, int charHeight, int spacing)
{
    if (!str)
        return;

    int textWidth = (strlen(str) * charWidth) + ((strlen(str) - 1) * spacing);
    int startX = rightX - textWidth;

    drawString(renderer, str, startX, y, charWidth, charHeight, spacing);
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

    // Save score before cleanup
    saveScore();

    // Cleanup resources
    cleanupGame();

    // Wait for spawner thread to finish
    pthread_join(spawnerThread, NULL);

    // Wait for child process
    wait(NULL);

    return 0;
}