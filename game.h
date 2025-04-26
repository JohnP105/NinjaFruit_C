#ifndef GAME_H
#define GAME_H

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
extern GameObject gameObjects[MAX_FRUITS];
extern pthread_mutex_t game_mutex;
extern int score;
extern int health;
extern int game_time;
extern Uint32 start_time;
extern int running;
extern int spawn_pipe[2];
extern GameState game_state;
extern ScoreRecord leaderboard[MAX_SCORES];
extern int num_scores;

// Deadlock detection globals
extern DeadlockDetector deadlock_detector;
extern pthread_t deadlock_thread;
extern int resources_held[MAX_RESOURCES];
extern int resource_request_probability;

// SDL related variables
extern SDL_Window *window;
extern SDL_Renderer *renderer;
extern SDL_Texture *background_texture;

// Sound effects
extern Mix_Chunk *sliceSound;
extern Mix_Chunk *bombSound;
extern Mix_Music *backgroundMusic;

// Mouse tracking
extern int mouse_x, mouse_y;
extern int prev_mouse_x, prev_mouse_y;
extern int mouse_down;

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
void drawString(SDL_Renderer *renderer, const char *str, int x, int y, int charWidth, int charHeight, int spacing);
void drawCenteredString(SDL_Renderer *renderer, const char *str, int centerX, int y, int charWidth, int charHeight, int spacing);
void drawRightAlignedString(SDL_Renderer *renderer, const char *str, int rightX, int y, int charWidth, int charHeight, int spacing);

// Deadlock detection functions
void initDeadlockDetector();
void cleanupDeadlockDetector();
int requestResource(int process_id, int resource_id, int amount);
void releaseResource(int process_id, int resource_id, int amount);
int detectDeadlock();
void recoverFromDeadlock();
void checkPowerUps();

#endif /* GAME_H */