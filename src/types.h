#ifndef SHMUP_TYPES_H
#define SHMUP_TYPES_H

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>

/* ========== Screen & GOP ========== */

#define SCREEN_W  800
#define SCREEN_H  600

/* ========== Color helpers (BGR) ========== */

#define COLOR(r,g,b)  ((UINT32)(((UINT8)(b) << 16) | ((UINT8)(g) << 8) | (UINT8)(r)))

#define CLR_BLACK       COLOR(0,0,0)
#define CLR_WHITE       COLOR(255,255,255)
#define CLR_RED         COLOR(255,50,50)
#define CLR_DARKRED     COLOR(180,20,20)
#define CLR_GREEN       COLOR(50,255,80)
#define CLR_DARKGREEN   COLOR(20,120,40)
#define CLR_BLUE        COLOR(60,120,255)
#define CLR_DARKBLUE    COLOR(20,40,160)
#define CLR_YELLOW      COLOR(255,255,60)
#define CLR_ORANGE      COLOR(255,160,40)
#define CLR_CYAN        COLOR(60,240,255)
#define CLR_PURPLE      COLOR(200,60,255)
#define CLR_PINK        COLOR(255,100,180)
#define CLR_GRAY        COLOR(128,128,128)
#define CLR_DARKGRAY    COLOR(40,40,50)
#define CLR_UI_BORDER   COLOR(80,80,140)
#define CLR_UI_ACCENT   COLOR(100,200,255)
#define CLR_ENEMY_BODY  COLOR(255,80,60)
#define CLR_ENEMY_WING  COLOR(200,40,30)
#define CLR_PLAYER_BODY COLOR(60,180,255)
#define CLR_PLAYER_WING COLOR(30,120,200)
#define CLR_PLAYER_COCK COLOR(200,230,255)
#define CLR_BULLET      COLOR(255,255,100)
#define CLR_ENEMY_BULLET COLOR(255,100,100)
#define CLR_EXPLOSION   COLOR(255,200,50)
#define CLR_HP_BAR      COLOR(50,255,80)
#define CLR_HP_BG       COLOR(60,20,20)
#define CLR_BOSS_BODY   COLOR(200,50,200)
#define CLR_BOSS_WING   COLOR(150,30,150)

/* ========== Game constants ========== */

#define PLAYER_W          30
#define PLAYER_H          36
#define PLAYER_SPEED      7
#define PLAYER_MAX_HP     5
#define PLAYER_FIRE_RATE  5
#define PLAYER_INVINCIBLE 45

#define BULLET_W          4
#define BULLET_H          12
#define BULLET_SPEED      12
#define ENEMY_BULLET_SPD  5

#define MAX_PLAYER_BULLETS  20
#define MAX_ENEMY_BULLETS   30
#define MAX_ENEMIES         15
#define MAX_PARTICLES       60
#define MAX_STARS           80

#define ENEMY_SMALL_W      24
#define ENEMY_SMALL_H      24
#define ENEMY_MED_W        36
#define ENEMY_MED_H        30
#define ENEMY_LARGE_W      48
#define ENEMY_LARGE_H      40

#define BOSS_W             80
#define BOSS_H             60

#define MAX_POWERUPS        10
#define POWERUP_SIZE        16
#define POWERUP_SPEED       2
#define MAX_WEAPON_LEVEL    4
#define BOMB_DURATION       30
#define MAX_BOMBS           5
#define SHIELD_DURATION     180

#define MAX_WINGMEN         2
#define WINGMAN_W           14
#define WINGMAN_H           18
#define WINGMAN_OFFSET_X    28
#define WINGMAN_OFFSET_Y    12

/* ========== Game states ========== */

typedef enum {
  STATE_TITLE,
  STATE_PLAYING,
  STATE_GAMEOVER
} GameStateEnum;

/* ========== Entity types ========== */

typedef struct {
  INT32 x, y;
  INT32 w, h;
  INT32 speed;
  INT32 hp;
  INT32 maxHp;
  INT32 fireTimer;
  INT32 fireRate;
  INT32 invTimer;
  INT32 animFrame;
  INT32 score;
  INT32 lives;
  INT32 weaponLevel;
  INT32 bombCount;
  INT32 shieldTimer;
  INT32 bombTimer;
} Player;

typedef enum {
  ENEMY_SMALL,
  ENEMY_MEDIUM,
  ENEMY_LARGE,
  ENEMY_BOSS
} EnemyType;

typedef enum {
  POWERUP_WEAPON,
  POWERUP_SHIELD,
  POWERUP_BOMB,
  POWERUP_LIFE,
  POWERUP_HEAL
} PowerUpType;

typedef struct {
  INT32 x, y;
  INT32 w, h;
  INT32 speed;
  INT32 hp;
  INT32 maxHp;
  INT32 fireTimer;
  INT32 fireRate;
  INT32 scoreValue;
  EnemyType type;
  INT32 moveTimer;
  BOOLEAN active;
} Enemy;

typedef struct {
  INT32 x, y;
  INT32 speed;
  INT32 dx;
  BOOLEAN active;
  BOOLEAN isEnemy;
  BOOLEAN isWingman;
} Bullet;

typedef struct {
  INT32 x, y;
  INT32 vx, vy;
  INT32 life;
  INT32 maxLife;
  UINT32 color;
  INT32 size;
  BOOLEAN active;
} Particle;

typedef struct {
  INT32 x, y;
  INT32 speed;
  INT32 brightness;
} Star;

typedef struct {
  INT32 x, y;
  INT32 speed;
  PowerUpType type;
  BOOLEAN active;
  INT32 animTimer;
} PowerUp;

typedef struct {
  INT32 x, y;
  INT32 fireTimer;
  INT32 side;       /* -1 = left, +1 = right */
  BOOLEAN active;
} Wingman;

typedef struct {
  GameStateEnum state;
  Player player;
  Enemy enemies[MAX_ENEMIES];
  Bullet pBullets[MAX_PLAYER_BULLETS];
  Bullet eBullets[MAX_ENEMY_BULLETS];
  Particle particles[MAX_PARTICLES];
  Star stars[MAX_STARS];
  INT32 frameCount;
  INT32 spawnTimer;
  INT32 spawnInterval;
  INT32 difficulty;
  INT32 level;
  INT32 killCount;
  INT32 bossSpawnThreshold;
  BOOLEAN bossActive;
  BOOLEAN quitRequested;
  INT32 titleAnim;
  INT32 gameOverTimer;
  PowerUp powerUps[MAX_POWERUPS];
  INT32 bombFlash;
  Wingman wingmen[MAX_WINGMEN];
} GameData;

/* ========== Function prototypes ========== */

/* render.c */
extern EFI_STATUS GraphicsInit(VOID);
extern VOID GraphicsCleanup(VOID);
extern VOID Present(VOID);
extern VOID ClearBackBuffer(UINT32 color);
extern VOID FillRect(INT32 x, INT32 y, INT32 w, INT32 h, UINT32 color);
extern VOID DrawCnText(INT32 x, INT32 y, const CHAR8 *text, UINT32 color, INT32 scale);
extern VOID DrawCnNum(INT32 x, INT32 y, INT32 num, UINT32 color, INT32 scale);
extern VOID RenderFrame(GameData *g);

/* game.c */
extern VOID GameInit(GameData *g);
extern VOID GameUpdate(GameData *g);
extern UINT32 ShmupRand(VOID);
extern VOID ShmupSeed(UINT32 seed);
extern VOID InitStars(GameData *g);
extern VOID ActivateBomb(GameData *g);

/* input.c */
extern VOID InputInit(VOID);
extern VOID InputUpdate(GameData *g);

#endif /* SHMUP_TYPES_H */
