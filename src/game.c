#include "types.h"

/* ========== Simple LCG PRNG ========== */

static UINT32 mSeed = 12345;

UINT32 ShmupRand(VOID)
{
  mSeed = mSeed * 1103515245u + 12345u;
  return (mSeed >> 16) & 0x7FFF;
}

VOID ShmupSeed(UINT32 seed)
{
  mSeed = seed;
}

/* ========== Collision ========== */

static BOOLEAN RectOverlap(INT32 ax, INT32 ay, INT32 aw, INT32 ah,
                           INT32 bx, INT32 by, INT32 bw, INT32 bh)
{
  return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

/* ========== Particle helpers ========== */

static VOID SpawnOneParticle(GameData *g, INT32 cx, INT32 cy, UINT32 baseColor)
{
  INT32 i;
  INT32 spd;
  for (i = 0; i < MAX_PARTICLES; i++) {
    if (!g->particles[i].active) {
      spd = 1 + (INT32)(ShmupRand() % 4);
      g->particles[i].vx = (spd * (INT32)(ShmupRand() % 200 - 100)) / 100;
      g->particles[i].vy = (spd * (INT32)(ShmupRand() % 200 - 100)) / 100;
      g->particles[i].x = cx;
      g->particles[i].y = cy;
      g->particles[i].life = 15 + (INT32)(ShmupRand() % 20);
      g->particles[i].maxLife = g->particles[i].life;
      g->particles[i].color = baseColor;
      g->particles[i].size = 2 + (INT32)(ShmupRand() % 3);
      g->particles[i].active = TRUE;
      return;
    }
  }
}

/* ========== Star field ========== */

VOID InitStars(GameData *g)
{
  INT32 i;
  for (i = 0; i < MAX_STARS; i++) {
    g->stars[i].x = (INT32)(ShmupRand() % SCREEN_W);
    g->stars[i].y = (INT32)(ShmupRand() % SCREEN_H);
    g->stars[i].speed = 1 + (INT32)(ShmupRand() % 3);
    g->stars[i].brightness = 80 + (INT32)(ShmupRand() % 120);
  }
}

static VOID UpdateStars(GameData *g)
{
  INT32 i;
  for (i = 0; i < MAX_STARS; i++) {
    g->stars[i].y += g->stars[i].speed;
    if (g->stars[i].y >= SCREEN_H) {
      g->stars[i].y = 0;
      g->stars[i].x = (INT32)(ShmupRand() % SCREEN_W);
    }
  }
}

/* ========== Power-up system ========== */

static VOID SpawnPowerUp(GameData *g, INT32 x, INT32 y)
{
  INT32 i;
  INT32 r = (INT32)(ShmupRand() % 100);
  PowerUpType type;

  if (r < 45)      type = POWERUP_WEAPON;
  else if (r < 60) type = POWERUP_SHIELD;
  else if (r < 75) type = POWERUP_BOMB;
  else if (r < 82) type = POWERUP_LIFE;
  else             type = POWERUP_HEAL;

  for (i = 0; i < MAX_POWERUPS; i++) {
    if (!g->powerUps[i].active) {
      g->powerUps[i].x = x;
      g->powerUps[i].y = y;
      g->powerUps[i].speed = POWERUP_SPEED;
      g->powerUps[i].type = type;
      g->powerUps[i].active = TRUE;
      g->powerUps[i].animTimer = 0;
      return;
    }
  }
}

static VOID UpdatePowerUps(GameData *g)
{
  INT32 i;
  for (i = 0; i < MAX_POWERUPS; i++) {
    if (!g->powerUps[i].active) continue;
    g->powerUps[i].y += g->powerUps[i].speed;
    g->powerUps[i].animTimer++;
    if (g->powerUps[i].y > SCREEN_H + POWERUP_SIZE) {
      g->powerUps[i].active = FALSE;
    }
  }
}

static VOID CheckPowerUpCollection(GameData *g)
{
  INT32 i;
  INT32 k;
  Player *p = &g->player;

  for (i = 0; i < MAX_POWERUPS; i++) {
    PowerUp *pu = &g->powerUps[i];
    if (!pu->active) continue;

    if (RectOverlap(p->x + 2, p->y + 2, PLAYER_W - 4, PLAYER_H - 4,
                    pu->x, pu->y, POWERUP_SIZE, POWERUP_SIZE)) {
      pu->active = FALSE;

      switch (pu->type) {
      case POWERUP_WEAPON:
        if (p->weaponLevel < MAX_WEAPON_LEVEL) {
          p->weaponLevel++;
        } else {
          /* Max weapon bonus: score + particles */
          p->score += 500;
          for (k = 0; k < 20; k++) {
            SpawnOneParticle(g, p->x + PLAYER_W / 2, p->y + PLAYER_H / 2, CLR_YELLOW);
          }
        }
        break;
      case POWERUP_SHIELD:
        p->shieldTimer = SHIELD_DURATION;
        break;
      case POWERUP_BOMB:
        if (p->bombCount < MAX_BOMBS) p->bombCount++;
        break;
      case POWERUP_LIFE:
        p->lives++;
        break;
      case POWERUP_HEAL:
        p->hp += 2;
        if (p->hp > p->maxHp) p->hp = p->maxHp;
        break;
      default:
        break;
      }

      /* Collection particles */
      {
        INT32 k;
        for (k = 0; k < 8; k++) {
          SpawnOneParticle(g, pu->x + POWERUP_SIZE / 2, pu->y + POWERUP_SIZE / 2,
            COLOR(255, 255, 100));
        }
      }
    }
  }
}

/* ========== Bomb ========== */

VOID ActivateBomb(GameData *g)
{
  INT32 i;
  INT32 k;

  g->player.bombCount--;
  g->bombFlash = BOMB_DURATION;

  /* Destroy all enemy bullets */
  for (i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (g->eBullets[i].active) {
      g->eBullets[i].active = FALSE;
    }
  }

  /* Damage all enemies */
  for (i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &g->enemies[i];
    if (!e->active) continue;
    e->hp -= 15;
    if (e->hp <= 0) {
      e->active = FALSE;
      g->player.score += e->scoreValue;
      g->killCount++;
      for (k = 0; k < 15; k++) {
        UINT32 colors[4];
        colors[0] = CLR_EXPLOSION; colors[1] = CLR_ORANGE;
        colors[2] = CLR_RED; colors[3] = CLR_WHITE;
        SpawnOneParticle(g, e->x + (INT32)(ShmupRand() % e->w),
                         e->y + (INT32)(ShmupRand() % e->h), colors[k % 4]);
      }
      if (e->type == ENEMY_BOSS) g->bossActive = FALSE;
    } else {
      /* Hit flash particles */
      for (k = 0; k < 5; k++) {
        SpawnOneParticle(g, e->x + e->w / 2, e->y + e->h / 2, CLR_WHITE);
      }
    }
  }

  /* Big center explosion */
  for (k = 0; k < 30; k++) {
    UINT32 colors[4];
    colors[0] = CLR_WHITE; colors[1] = CLR_YELLOW;
    colors[2] = CLR_CYAN; colors[3] = CLR_EXPLOSION;
    SpawnOneParticle(g, SCREEN_W / 2 + (INT32)(ShmupRand() % 200) - 100,
                     SCREEN_H / 2 + (INT32)(ShmupRand() % 200) - 100, colors[k % 4]);
  }
}

/* ========== Enemy spawning ========== */

static VOID SpawnEnemy(GameData *g, EnemyType type)
{
  INT32 i;
  for (i = 0; i < MAX_ENEMIES; i++) {
    if (!g->enemies[i].active) {
      Enemy *e = &g->enemies[i];
      SetMem(e, sizeof(Enemy), 0);
      e->type = type;
      e->active = TRUE;

      switch (type) {
      case ENEMY_SMALL:
        e->w = ENEMY_SMALL_W;
        e->h = ENEMY_SMALL_H;
        e->hp = 1;
        e->maxHp = 1;
        e->speed = 3 + (INT32)(ShmupRand() % 2);
        e->fireRate = 70 + (INT32)(ShmupRand() % 50);
        e->scoreValue = 100;
        break;
      case ENEMY_MEDIUM:
        e->w = ENEMY_MED_W;
        e->h = ENEMY_MED_H;
        e->hp = 3;
        e->maxHp = 3;
        e->speed = 2 + (INT32)(ShmupRand() % 2);
        e->fireRate = 40 + (INT32)(ShmupRand() % 30);
        e->scoreValue = 300;
        break;
      case ENEMY_LARGE:
        e->w = ENEMY_LARGE_W;
        e->h = ENEMY_LARGE_H;
        e->hp = 8;
        e->maxHp = 8;
        e->speed = 1;
        e->fireRate = 25 + (INT32)(ShmupRand() % 15);
        e->scoreValue = 500;
        break;
      case ENEMY_BOSS:
        e->w = BOSS_W;
        e->h = BOSS_H;
        e->hp = 50 + g->level * 10;
        e->maxHp = e->hp;
        e->speed = 1;
        e->fireRate = 12;
        e->scoreValue = 2000;
        break;
      default:
        break;
      }
      e->fireTimer = e->fireRate;

      if (type == ENEMY_BOSS) {
        e->x = SCREEN_W / 2 - BOSS_W / 2;
        e->y = -BOSS_H;
      } else {
        e->x = 10 + (INT32)(ShmupRand() % (SCREEN_W - e->w - 20));
        e->y = -e->h - (INT32)(ShmupRand() % 40);
      }
      return;
    }
  }
}

/* ========== Enemy update ========== */

static VOID UpdateEnemies(GameData *g)
{
  INT32 i;
  INT32 j;
  for (i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &g->enemies[i];
    if (!e->active) continue;

    e->moveTimer++;

    switch (e->type) {
    case ENEMY_SMALL:
      e->y += e->speed;
      e->x += ((e->moveTimer / 15) % 2 == 0) ? 2 : -2;
      break;
    case ENEMY_MEDIUM:
      e->y += e->speed;
      e->x += 2 * ((e->moveTimer / 25) % 3) - 1;
      break;
    case ENEMY_LARGE:
      e->y += e->speed;
      e->x += (INT32)(ShmupRand() % 3) - 1;
      break;
    case ENEMY_BOSS:
      if (e->y < 40) {
        e->y += 1;
      } else {
        e->x += ((e->moveTimer / 40) % 2 == 0) ? 2 : -2;
        if (e->x < 10) e->x = 10;
        if (e->x + e->w > SCREEN_W - 10) e->x = SCREEN_W - 10 - e->w;
      }
      break;
    default:
      break;
    }

    /* Fire */
    e->fireTimer--;
    if (e->fireTimer <= 0 && e->y > 0) {
      INT32 shots = 1;
      INT32 offsets[3];
      INT32 s;
      offsets[0] = e->x + e->w / 2 - 2;

      if (e->type == ENEMY_BOSS) {
        shots = 3;
        offsets[0] = e->x + 10;
        offsets[1] = e->x + e->w / 2 - 2;
        offsets[2] = e->x + e->w - 14;
      } else if (e->type == ENEMY_LARGE) {
        shots = 2;
        offsets[0] = e->x + 5;
        offsets[1] = e->x + e->w - 10;
      }

      for (s = 0; s < shots; s++) {
        for (j = 0; j < MAX_ENEMY_BULLETS; j++) {
          if (!g->eBullets[j].active) {
            g->eBullets[j].x = offsets[s];
            g->eBullets[j].y = e->y + e->h;
            g->eBullets[j].speed = ENEMY_BULLET_SPD;
            g->eBullets[j].dx = 0;
            g->eBullets[j].active = TRUE;
            g->eBullets[j].isEnemy = TRUE;
            /* Boss shots aimed at player */
            if (e->type == ENEMY_BOSS && s != 1) {
              INT32 dx = g->player.x + PLAYER_W / 2 - offsets[s];
              g->eBullets[j].dx = (dx > 0) ? 1 : (dx < 0) ? -1 : 0;
            }
            break;
          }
        }
      }
      e->fireTimer = e->fireRate;
    }

    if (e->y > SCREEN_H + 20) {
      if (e->type == ENEMY_BOSS) g->bossActive = FALSE;
      e->active = FALSE;
    }
  }
}

/* ========== Bullet update ========== */

static VOID UpdateBullets(GameData *g)
{
  INT32 i;

  for (i = 0; i < MAX_PLAYER_BULLETS; i++) {
    if (g->pBullets[i].active) {
      g->pBullets[i].y -= g->pBullets[i].speed;
      g->pBullets[i].x += g->pBullets[i].dx;
      if (g->pBullets[i].y + BULLET_H < 0 ||
          g->pBullets[i].x < -BULLET_W || g->pBullets[i].x > SCREEN_W) {
        g->pBullets[i].active = FALSE;
      }
    }
  }

  for (i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (g->eBullets[i].active) {
      g->eBullets[i].y += g->eBullets[i].speed;
      g->eBullets[i].x += g->eBullets[i].dx;
      if (g->eBullets[i].y > SCREEN_H ||
          g->eBullets[i].x < -4 || g->eBullets[i].x > SCREEN_W) {
        g->eBullets[i].active = FALSE;
      }
    }
  }
}

/* ========== Particle update ========== */

static VOID UpdateParticles(GameData *g)
{
  INT32 i;
  for (i = 0; i < MAX_PARTICLES; i++) {
    Particle *p = &g->particles[i];
    if (!p->active) continue;
    p->x += p->vx;
    p->y += p->vy;
    p->life--;
    if (p->life <= 0) p->active = FALSE;
  }
}

/* ========== Collision detection ========== */

static VOID CheckCollisions(GameData *g)
{
  INT32 i, j, k;
  Player *pl = &g->player;

  /* Player bullets vs enemies */
  for (i = 0; i < MAX_PLAYER_BULLETS; i++) {
    if (!g->pBullets[i].active) continue;
    for (j = 0; j < MAX_ENEMIES; j++) {
      Enemy *e = &g->enemies[j];
      if (!e->active) continue;

      if (RectOverlap(g->pBullets[i].x, g->pBullets[i].y, BULLET_W, BULLET_H,
                      e->x, e->y, e->w, e->h)) {
        g->pBullets[i].active = FALSE;
        e->hp--;
        SpawnOneParticle(g, g->pBullets[i].x, g->pBullets[i].y, CLR_ORANGE);

        if (e->hp <= 0) {
          e->active = FALSE;
          pl->score += e->scoreValue;
          g->killCount++;
          for (k = 0; k < 12; k++) {
            UINT32 colors[4];
            colors[0] = CLR_EXPLOSION; colors[1] = CLR_ORANGE;
            colors[2] = CLR_RED; colors[3] = CLR_YELLOW;
            SpawnOneParticle(g, e->x + e->w / 2, e->y + e->h / 2, colors[k % 4]);
          }
          if (e->type == ENEMY_BOSS) {
            g->bossActive = FALSE;
            for (k = 0; k < 30; k++) {
              UINT32 colors[6];
              colors[0] = CLR_EXPLOSION; colors[1] = CLR_ORANGE; colors[2] = CLR_RED;
              colors[3] = CLR_YELLOW; colors[4] = CLR_WHITE; colors[5] = CLR_PINK;
              SpawnOneParticle(g, e->x + (INT32)(ShmupRand() % e->w),
                               e->y + (INT32)(ShmupRand() % e->h), colors[k % 6]);
            }
          }

          /* Drop power-up */
          {
            INT32 dropChance = 35;
            if (e->type == ENEMY_MEDIUM) dropChance = 55;
            else if (e->type == ENEMY_LARGE) dropChance = 75;
            else if (e->type == ENEMY_BOSS) dropChance = 100;

            if ((INT32)(ShmupRand() % 100) < dropChance) {
              SpawnPowerUp(g, e->x + e->w / 2 - POWERUP_SIZE / 2, e->y + e->h / 2);
            }
            /* Boss drops extra items */
            if (e->type == ENEMY_BOSS) {
              SpawnPowerUp(g, e->x + 10, e->y + 20);
              SpawnPowerUp(g, e->x + e->w - 26, e->y + 20);
            }
          }
        }
        break;
      }
    }
  }

  /* Enemy bullets vs player */
  if (pl->invTimer <= 0 && pl->shieldTimer <= 0) {
    for (i = 0; i < MAX_ENEMY_BULLETS; i++) {
      if (!g->eBullets[i].active) continue;
      if (RectOverlap(g->eBullets[i].x, g->eBullets[i].y, 4, 4,
                      pl->x + 4, pl->y + 4, PLAYER_W - 8, PLAYER_H - 8)) {
        g->eBullets[i].active = FALSE;
        pl->hp--;
        pl->invTimer = PLAYER_INVINCIBLE;
        for (k = 0; k < 6; k++) {
          SpawnOneParticle(g, pl->x + PLAYER_W / 2, pl->y + PLAYER_H / 2, CLR_RED);
        }
        if (pl->hp <= 0) {
          pl->lives--;
          if (pl->lives <= 0) {
            g->state = STATE_GAMEOVER;
            g->gameOverTimer = 0;
            for (k = 0; k < 20; k++) {
              UINT32 colors[5];
              colors[0] = CLR_EXPLOSION; colors[1] = CLR_ORANGE; colors[2] = CLR_RED;
              colors[3] = CLR_YELLOW; colors[4] = CLR_WHITE;
              SpawnOneParticle(g, pl->x + PLAYER_W / 2, pl->y + PLAYER_H / 2, colors[k % 5]);
            }
            return;
          }
          pl->hp = pl->maxHp;
          pl->invTimer = PLAYER_INVINCIBLE * 2;
        }
        break;
      }
    }
  } else if (pl->shieldTimer > 0) {
    /* Shield absorbs enemy bullets */
    for (i = 0; i < MAX_ENEMY_BULLETS; i++) {
      if (!g->eBullets[i].active) continue;
      if (RectOverlap(g->eBullets[i].x, g->eBullets[i].y, 4, 4,
                      pl->x - 4, pl->y - 4, PLAYER_W + 8, PLAYER_H + 8)) {
        g->eBullets[i].active = FALSE;
        SpawnOneParticle(g, g->eBullets[i].x, g->eBullets[i].y, CLR_CYAN);
      }
    }
  }

  /* Enemy body vs player */
  if (pl->invTimer <= 0 && pl->shieldTimer <= 0) {
    for (j = 0; j < MAX_ENEMIES; j++) {
      Enemy *e = &g->enemies[j];
      if (!e->active) continue;
      if (RectOverlap(pl->x + 4, pl->y + 4, PLAYER_W - 8, PLAYER_H - 8,
                      e->x, e->y, e->w, e->h)) {
        pl->hp -= 2;
        pl->invTimer = PLAYER_INVINCIBLE;
        e->hp -= 3;
        for (k = 0; k < 8; k++) {
          SpawnOneParticle(g, (pl->x + e->x + e->w) / 2, (pl->y + e->y + e->h) / 2, CLR_WHITE);
        }
        if (e->hp <= 0) {
          e->active = FALSE;
          pl->score += e->scoreValue / 2;
          g->killCount++;
          if (e->type == ENEMY_BOSS) g->bossActive = FALSE;
        }
        if (pl->hp <= 0) {
          pl->lives--;
          if (pl->lives <= 0) {
            g->state = STATE_GAMEOVER;
            g->gameOverTimer = 0;
            return;
          }
          pl->hp = pl->maxHp;
          pl->invTimer = PLAYER_INVINCIBLE * 2;
        }
        break;
      }
    }
  }
}

/* ========== Game init & update ========== */

VOID GameInit(GameData *g)
{
  SetMem(g, sizeof(GameData), 0);

  mSeed = 12345;

  g->state = STATE_PLAYING;
  g->player.x = SCREEN_W / 2 - PLAYER_W / 2;
  g->player.y = SCREEN_H - PLAYER_H - 40;
  g->player.w = PLAYER_W;
  g->player.h = PLAYER_H;
  g->player.hp = PLAYER_MAX_HP;
  g->player.maxHp = PLAYER_MAX_HP;
  g->player.lives = 3;
  g->player.fireRate = PLAYER_FIRE_RATE;
  g->player.fireTimer = 0;
  g->player.score = 0;
  g->player.invTimer = PLAYER_INVINCIBLE;
  g->player.weaponLevel = 0;
  g->player.bombCount = 1;
  g->player.shieldTimer = 0;
  g->player.bombTimer = 0;

  g->frameCount = 0;
  g->spawnTimer = 0;
  g->spawnInterval = 35;
  g->difficulty = 1;
  g->level = 1;
  g->killCount = 0;
  g->bossSpawnThreshold = 20;
  g->bossActive = FALSE;
  g->quitRequested = FALSE;
  g->gameOverTimer = 0;
  g->bombFlash = 0;

  InitStars(g);
}

VOID GameUpdate(GameData *g)
{
  INT32 r;
  INT32 newLevel;

  if (g->state == STATE_TITLE) {
    g->titleAnim++;
    UpdateStars(g);
    return;
  }

  if (g->state == STATE_GAMEOVER) {
    g->gameOverTimer++;
    g->frameCount++;
    UpdateStars(g);
    UpdateParticles(g);
    return;
  }

  /* STATE_PLAYING */
  g->frameCount++;
  g->player.animFrame++;
  if (g->player.invTimer > 0) g->player.invTimer--;
  if (g->player.fireTimer > 0) g->player.fireTimer--;
  if (g->player.shieldTimer > 0) g->player.shieldTimer--;
  if (g->player.bombTimer > 0) g->player.bombTimer--;

  /* Bomb flash countdown */
  if (g->bombFlash > 0) g->bombFlash--;

  /* Wingmen update - follow player */
  {
    Player *pp = &g->player;
    INT32 wi;
    for (wi = 0; wi < MAX_WINGMEN; wi++) {
      Wingman *wm = &g->wingmen[wi];
      if (pp->weaponLevel >= 2) {
        INT32 targetX, targetY;
        wm->active = TRUE;
        wm->side = (wi == 0) ? -1 : 1;
        targetX = pp->x + PLAYER_W / 2 + wm->side * WINGMAN_OFFSET_X - WINGMAN_W / 2;
        targetY = pp->y + WINGMAN_OFFSET_Y;
        /* Smooth follow */
        wm->x += (targetX - wm->x) / 3;
        wm->y += (targetY - wm->y) / 3;
        if (wm->fireTimer > 0) wm->fireTimer--;
      } else {
        wm->active = FALSE;
      }
    }
  }

  UpdateStars(g);
  UpdateBullets(g);
  UpdateEnemies(g);
  UpdatePowerUps(g);
  CheckPowerUpCollection(g);
  UpdateParticles(g);
  CheckCollisions(g);

  /* Spawn enemies */
  g->spawnTimer++;
  if (g->spawnTimer >= g->spawnInterval) {
    g->spawnTimer = 0;
    if (!g->bossActive) {
      r = (INT32)(ShmupRand() % 100);
      if (r < 50)       SpawnEnemy(g, ENEMY_SMALL);
      else if (r < 80)  SpawnEnemy(g, ENEMY_MEDIUM);
      else              SpawnEnemy(g, ENEMY_LARGE);
    }
  }

  /* Boss spawn */
  if (g->killCount >= g->bossSpawnThreshold && !g->bossActive) {
    SpawnEnemy(g, ENEMY_BOSS);
    g->bossActive = TRUE;
    g->bossSpawnThreshold += 25 + g->level * 5;
  }

  /* Level up */
  newLevel = g->player.score / 3000 + 1;
  if (newLevel > g->level) {
    g->level = newLevel;
    g->spawnInterval = 35 - g->level * 2;
    if (g->spawnInterval < 12) g->spawnInterval = 12;
  }
}
