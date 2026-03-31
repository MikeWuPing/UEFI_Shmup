#include "types.h"

/* ========== Input handling ========== */

VOID InputInit(VOID)
{
  EFI_INPUT_KEY Key;
  /* Drain key buffer */
  while (!EFI_ERROR(gST->ConIn->ReadKeyStroke(gST->ConIn, &Key))) {
    /* discard */
  }
}

/* Helper: spawn one player bullet at (x,y) with horizontal speed dx */
static VOID SpawnPBullet(GameData *g, INT32 x, INT32 y, INT32 dx)
{
  INT32 i;
  for (i = 0; i < MAX_PLAYER_BULLETS; i++) {
    if (!g->pBullets[i].active) {
      g->pBullets[i].x = x;
      g->pBullets[i].y = y;
      g->pBullets[i].speed = BULLET_SPEED;
      g->pBullets[i].dx = dx;
      g->pBullets[i].active = TRUE;
      g->pBullets[i].isEnemy = FALSE;
      g->pBullets[i].isWingman = FALSE;
      return;
    }
  }
}

VOID InputUpdate(GameData *g)
{
  EFI_INPUT_KEY Key;
  EFI_STATUS Status;
  BOOLEAN startPressed = FALSE;
  BOOLEAN quitPressed = FALSE;
  BOOLEAN firePressed = FALSE;
  BOOLEAN bombPressed = FALSE;
  BOOLEAN moveLeft = FALSE;
  BOOLEAN moveRight = FALSE;
  BOOLEAN moveUp = FALSE;
  BOOLEAN moveDown = FALSE;

  /* Read all available keys */
  while (TRUE) {
    Status = gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);
    if (EFI_ERROR(Status)) break;

    if (Key.ScanCode == 0x01)      moveUp = TRUE;
    else if (Key.ScanCode == 0x02) moveDown = TRUE;
    else if (Key.ScanCode == 0x03) moveRight = TRUE;
    else if (Key.ScanCode == 0x04) moveLeft = TRUE;
    else if (Key.ScanCode == 0x17) quitPressed = TRUE;

    if (Key.UnicodeChar == L' ')             firePressed = TRUE;
    else if (Key.UnicodeChar == 0x0D)        startPressed = TRUE;
    else if (Key.UnicodeChar == L'x' ||
             Key.UnicodeChar == L'X')        bombPressed = TRUE;
  }

  /* Apply to game state */
  if (g->state == STATE_TITLE) {
    if (startPressed) {
      GameInit(g);
      g->state = STATE_PLAYING;
    }
    if (quitPressed) g->quitRequested = TRUE;
    return;
  }

  if (g->state == STATE_GAMEOVER) {
    if (startPressed && g->gameOverTimer > 60) {
      g->state = STATE_TITLE;
      g->titleAnim = 0;
    }
    if (quitPressed) g->quitRequested = TRUE;
    return;
  }

  /* STATE_PLAYING */
  if (quitPressed) {
    g->quitRequested = TRUE;
    return;
  }

  {
    Player *p = &g->player;
    if (moveLeft)  p->x -= PLAYER_SPEED;
    if (moveRight) p->x += PLAYER_SPEED;
    if (moveUp)    p->y -= PLAYER_SPEED;
    if (moveDown)  p->y += PLAYER_SPEED;

    /* Clamp to screen */
    if (p->x < 0) p->x = 0;
    if (p->x + PLAYER_W > SCREEN_W) p->x = SCREEN_W - PLAYER_W;
    if (p->y < 34) p->y = 34;
    if (p->y + PLAYER_H > SCREEN_H) p->y = SCREEN_H - PLAYER_H;

    /* Fire weapon based on weapon level */
    if (firePressed && p->fireTimer <= 0) {
      INT32 cx = p->x + PLAYER_W / 2;
      INT32 by = p->y - BULLET_H;
      INT32 fr = p->fireRate - (p->weaponLevel / 2);
      if (fr < 2) fr = 2;

      switch (p->weaponLevel) {
      case 0:
        /* Single center shot */
        SpawnPBullet(g, cx - 2, by, 0);
        break;
      case 1:
        /* Double parallel */
        SpawnPBullet(g, cx - 6, by, 0);
        SpawnPBullet(g, cx + 2, by, 0);
        break;
      case 2:
        /* Triple: center + slight spread */
        SpawnPBullet(g, cx - 2, by, 0);
        SpawnPBullet(g, cx - 8, by, -1);
        SpawnPBullet(g, cx + 4, by, 1);
        break;
      case 3:
        /* Quad: double straight + double spread */
        SpawnPBullet(g, cx - 5, by, 0);
        SpawnPBullet(g, cx + 1, by, 0);
        SpawnPBullet(g, cx - 10, by, -1);
        SpawnPBullet(g, cx + 6, by, 1);
        break;
      case 4:
        /* Full spread: 5 bullets */
        SpawnPBullet(g, cx - 2, by, 0);
        SpawnPBullet(g, cx - 7, by + 2, -1);
        SpawnPBullet(g, cx + 3, by + 2, 1);
        SpawnPBullet(g, cx - 13, by + 4, -2);
        SpawnPBullet(g, cx + 9, by + 4, 2);
        break;
      default:
        SpawnPBullet(g, cx - 2, by, 0);
        break;
      }
      p->fireTimer = fr;

      /* Wingmen fire when weapon level >= 2 */
      {
        INT32 wi;
        for (wi = 0; wi < MAX_WINGMEN; wi++) {
          Wingman *wm = &g->wingmen[wi];
          if (wm->active && wm->fireTimer <= 0) {
            INT32 bi;
            for (bi = 0; bi < MAX_PLAYER_BULLETS; bi++) {
              if (!g->pBullets[bi].active) {
                g->pBullets[bi].x = wm->x + WINGMAN_W / 2 - 2;
                g->pBullets[bi].y = wm->y - BULLET_H;
                g->pBullets[bi].speed = BULLET_SPEED;
                g->pBullets[bi].dx = 0;
                g->pBullets[bi].active = TRUE;
                g->pBullets[bi].isEnemy = FALSE;
                g->pBullets[bi].isWingman = TRUE;
                break;
              }
            }
            wm->fireTimer = fr + 2;
          }
        }
      }
    }

    /* Bomb */
    if (bombPressed && p->bombCount > 0 && p->bombTimer <= 0) {
      ActivateBomb(g);
      p->bombTimer = 20;
    }
  }
}
