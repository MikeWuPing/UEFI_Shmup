#include "types.h"
#include "cn_font.h"

/* ========== Graphics globals ========== */

static EFI_GRAPHICS_OUTPUT_PROTOCOL *mGop = NULL;
static UINT32 *mBackBuffer = NULL;
static UINT32 mScrW = 0;
static UINT32 mScrH = 0;

/* ========== Graphics init / cleanup ========== */

EFI_STATUS GraphicsInit(VOID)
{
  EFI_STATUS Status;
  UINT32 BestMode;
  UINT32 ModeIndex;
  UINTN InfoSize;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;

  Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&mGop);
  if (EFI_ERROR(Status)) return Status;

  BestMode = mGop->Mode->Mode;
  for (ModeIndex = 0; ModeIndex < mGop->Mode->MaxMode; ModeIndex++) {
    Status = mGop->QueryMode(mGop, ModeIndex, &InfoSize, &Info);
    if (!EFI_ERROR(Status) && Info->HorizontalResolution == SCREEN_W && Info->VerticalResolution == SCREEN_H) {
      BestMode = ModeIndex;
      break;
    }
  }

  Status = mGop->SetMode(mGop, BestMode);
  if (EFI_ERROR(Status)) return Status;

  mScrW = mGop->Mode->Info->HorizontalResolution;
  mScrH = mGop->Mode->Info->VerticalResolution;
  mBackBuffer = AllocatePool(mScrW * mScrH * sizeof(UINT32));
  if (mBackBuffer == NULL) return EFI_OUT_OF_RESOURCES;

  return EFI_SUCCESS;
}

VOID GraphicsCleanup(VOID)
{
  if (mBackBuffer != NULL) { FreePool(mBackBuffer); mBackBuffer = NULL; }
}

/* ========== Drawing primitives ========== */

VOID ClearBackBuffer(UINT32 color)
{
  UINTN i;
  UINTN total = (UINTN)mScrW * mScrH;
  for (i = 0; i < total; i++) mBackBuffer[i] = color;
}

VOID FillRect(INT32 x, INT32 y, INT32 w, INT32 h, UINT32 color)
{
  INT32 i, j;
  if (x + w <= 0 || y + h <= 0 || x >= (INT32)mScrW || y >= (INT32)mScrH) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > (INT32)mScrW) w = (INT32)mScrW - x;
  if (y + h > (INT32)mScrH) h = (INT32)mScrH - y;
  for (j = y; j < y + h; j++) {
    UINT32 *row = &mBackBuffer[j * mScrW + x];
    for (i = 0; i < w; i++) row[i] = color;
  }
}

/* Blit a 16x16 font glyph at scale */
static VOID BlitGlyph(INT32 x, INT32 y, const UINT8 *data, INT32 scale, UINT32 color)
{
  INT32 r, c;
  for (r = 0; r < 16; r++) {
    UINT8 b0 = data[r * 2];
    UINT8 b1 = data[r * 2 + 1];
    for (c = 0; c < 8; c++) {
      if (b0 & (0x80 >> c)) FillRect(x + c * scale, y + r * scale, scale, scale, color);
    }
    for (c = 0; c < 8; c++) {
      if (b1 & (0x80 >> c)) FillRect(x + (8 + c) * scale, y + r * scale, scale, scale, color);
    }
  }
}

/* Draw UTF-8 text (supports Chinese + ASCII from generated font) */
VOID DrawCnText(INT32 x, INT32 y, const CHAR8 *text, UINT32 color, INT32 scale)
{
  INT32 cx = x;
  while (*text) {
    UINT16 code;
    const FONT_ENTRY *e;
    UINT8 b0 = (UINT8)(*text);

    if (b0 < 0x80) {
      code = b0;
      text++;
    } else if ((b0 & 0xE0) == 0xC0) {
      code = (UINT16)((b0 & 0x1F) << 6);
      text++;
      if (*text) { code |= (UINT8)(*text) & 0x3F; text++; }
    } else if ((b0 & 0xF0) == 0xE0) {
      code = (UINT16)((b0 & 0x0F) << 12);
      text++;
      if (*text) { code |= ((UINT8)(*text) & 0x3F) << 6; text++; }
      if (*text) { code |= (UINT8)(*text) & 0x3F; text++; }
    } else {
      text++;
      continue;
    }

    e = FontLookup(code);
    if (e) {
      BlitGlyph(cx, y, e->Data, scale, color);
      cx += FONT_CHAR_W * scale;
    } else {
      cx += 8 * scale;
    }
  }
}

VOID DrawCnNum(INT32 x, INT32 y, INT32 num, UINT32 color, INT32 scale)
{
  CHAR8 buf[16];
  CHAR8 tmp[2];
  INT32 i = 0;
  INT32 j;

  tmp[1] = 0;
  if (num < 0) { DrawCnText(x, y, "-", color, scale); x += FONT_CHAR_W * scale; num = -num; }
  if (num == 0) { DrawCnText(x, y, "0", color, scale); return; }
  while (num > 0) { buf[i++] = (CHAR8)('0' + num % 10); num /= 10; }
  for (j = i - 1; j >= 0; j--) {
    tmp[0] = buf[j];
    DrawCnText(x, y, tmp, color, scale);
    x += FONT_CHAR_W * scale;
  }
}

/* ========== Present ========== */

VOID Present(VOID)
{
  if (mGop == NULL || mBackBuffer == NULL) return;
  mGop->Blt(mGop, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)mBackBuffer,
    EfiBltBufferToVideo, 0, 0, 0, 0, mScrW, mScrH, mScrW * sizeof(UINT32));
}

/* ================================================================== */
/*                     SPRITE RENDERING                               */
/* ================================================================== */

/* --- Gradient helpers --- */
static VOID FillRectGradV(INT32 x, INT32 y, INT32 w, INT32 h, UINT32 cTop, UINT32 cBot)
{
  INT32 i;
  for (i = 0; i < h; i++) {
    INT32 r0 = (cTop >> 0) & 0xFF, g0 = (cTop >> 8) & 0xFF, b0 = (cTop >> 16) & 0xFF;
    INT32 r1 = (cBot >> 0) & 0xFF, g1 = (cBot >> 8) & 0xFF, b1 = (cBot >> 16) & 0xFF;
    INT32 t = (h > 1) ? i * 256 / (h - 1) : 0;
    UINT32 c = COLOR(r0 + (r1 - r0) * t / 256, g0 + (g1 - g0) * t / 256, b0 + (b1 - b0) * t / 256);
    FillRect(x, y + i, w, 1, c);
  }
}

static VOID FillRectGradH(INT32 x, INT32 y, INT32 w, INT32 h, UINT32 cL, UINT32 cR)
{
  INT32 i;
  for (i = 0; i < w; i++) {
    INT32 r0 = (cL >> 0) & 0xFF, g0 = (cL >> 8) & 0xFF, b0 = (cL >> 16) & 0xFF;
    INT32 r1 = (cR >> 0) & 0xFF, g1 = (cR >> 8) & 0xFF, b1 = (cR >> 16) & 0xFF;
    INT32 t = (w > 1) ? i * 256 / (w - 1) : 0;
    UINT32 c = COLOR(r0 + (r1 - r0) * t / 256, g0 + (g1 - g0) * t / 256, b0 + (b1 - b0) * t / 256);
    FillRect(x + i, y, 1, h, c);
  }
}

/* --- Player ship: hyper-detailed F-22 style fighter with canards --- */
static VOID DrawPlayer(Player *p)
{
  INT32 cx = p->x + PLAYER_W / 2;
  INT32 by = p->y + PLAYER_H;
  INT32 ty = p->y;
  INT32 wl = p->weaponLevel;
  INT32 af = p->animFrame;

  /* === Layer 1: Engine exhaust effects === */
  {
    INT32 fl = 12 + (af % 4) * 5;
    INT32 fl2 = 8 + ((af + 2) % 5) * 3;
    INT32 fl3 = 5 + ((af + 1) % 3) * 3;
    /* Twin main engines - outer flame envelope */
    FillRectGradV(cx - 6, by, 6, fl + 2, COLOR(180, 80, 10), COLOR(80, 15, 0));
    FillRectGradV(cx, by, 6, fl + 2, COLOR(180, 80, 10), COLOR(80, 15, 0));
    /* Twin main engines - inner hot core */
    FillRectGradV(cx - 5, by, 4, fl, COLOR(255, 240, 100), COLOR(255, 50, 0));
    FillRectGradV(cx + 1, by, 4, fl, COLOR(255, 240, 100), COLOR(255, 50, 0));
    /* Engine core white-hot center */
    FillRect(cx - 4, by, 2, fl - 3, COLOR(255, 255, 220));
    FillRect(cx + 2, by, 2, fl - 3, COLOR(255, 255, 220));
    /* Engine nozzle ring segments */
    FillRect(cx - 6, by, 1, 3, COLOR(255, 160, 40));
    FillRect(cx + 5, by, 1, 3, COLOR(255, 160, 40));
    FillRect(cx - 3, by + 2, 1, 2, COLOR(255, 180, 60));
    FillRect(cx + 3, by + 2, 1, 2, COLOR(255, 180, 60));
    /* Wing tip thrusters with glow envelope */
    FillRectGradV(cx - 16, by, 3, fl2 + 2, COLOR(140, 60, 10), COLOR(60, 10, 0));
    FillRectGradV(cx + 13, by, 3, fl2 + 2, COLOR(140, 60, 10), COLOR(60, 10, 0));
    FillRectGradV(cx - 15, by - 2, 2, fl2, COLOR(255, 160, 40), COLOR(200, 25, 0));
    FillRectGradV(cx + 13, by - 2, 2, fl2, COLOR(255, 160, 40), COLOR(200, 25, 0));
    /* Maneuvering jets - alternating pair */
    if (af & 1) {
      FillRect(cx - 8, by + 4, 1, fl3, COLOR(255, 200, 80));
      FillRect(cx + 7, by + 4, 1, fl3, COLOR(255, 200, 80));
    } else {
      FillRect(cx - 10, by + 2, 1, fl3 - 1, COLOR(255, 180, 60));
      FillRect(cx + 9, by + 2, 1, fl3 - 1, COLOR(255, 180, 60));
    }
    /* Engine afterburner bloom */
    if (af % 3 == 0) {
      FillRect(cx - 3, by + fl - 2, 2, 3, COLOR(255, 100, 20));
      FillRect(cx + 1, by + fl - 2, 2, 3, COLOR(255, 100, 20));
    }
  }

  /* === Layer 2: Outer delta wings - expanded === */
  /* Wing shadow layer */
  FillRect(cx - 18, ty + 19, 8, 18, COLOR(6, 30, 80));
  FillRect(cx + 10, ty + 19, 8, 18, COLOR(6, 30, 80));
  /* Wing main surface with gradient */
  FillRectGradH(cx - 17, ty + 18, 8, 18, COLOR(35, 120, 220), COLOR(10, 45, 110));
  FillRectGradH(cx + 9, ty + 18, 8, 18, COLOR(10, 45, 110), COLOR(35, 120, 220));
  /* Wing upper highlight band */
  FillRect(cx - 16, ty + 19, 6, 12, COLOR(55, 170, 255));
  FillRect(cx + 10, ty + 19, 6, 12, COLOR(55, 170, 255));
  /* Wing leading edge bright strip */
  FillRect(cx - 17, ty + 18, 7, 1, COLOR(130, 220, 255));
  FillRect(cx + 10, ty + 18, 7, 1, COLOR(130, 220, 255));
  /* Wing trailing edge dark */
  FillRect(cx - 17, ty + 35, 8, 1, COLOR(4, 20, 60));
  FillRect(cx + 9, ty + 35, 8, 1, COLOR(4, 20, 60));
  /* Wing panel lines */
  FillRect(cx - 15, ty + 27, 5, 1, COLOR(15, 55, 130));
  FillRect(cx + 10, ty + 27, 5, 1, COLOR(15, 55, 130));
  /* Wing root fairing - thicker */
  FillRect(cx - 9, ty + 17, 2, 16, COLOR(22, 85, 175));
  FillRect(cx + 7, ty + 17, 2, 16, COLOR(22, 85, 175));
  /* Wing root fillet */
  FillRect(cx - 8, ty + 18, 1, 14, COLOR(40, 140, 230));
  FillRect(cx + 7, ty + 18, 1, 14, COLOR(40, 140, 230));
  /* Wing underside shadow */
  FillRect(cx - 17, ty + 33, 7, 2, COLOR(8, 40, 100));
  FillRect(cx + 10, ty + 33, 7, 2, COLOR(8, 40, 100));

  /* Navigation lights - pulsing */
  {
    UINT32 redL = (af & 3) ? COLOR(255, 30, 30) : COLOR(255, 100, 100);
    UINT32 grnL = (af & 3) ? COLOR(30, 255, 30) : COLOR(100, 255, 100);
    FillRect(cx - 17, ty + 18, 2, 2, redL);
    FillRect(cx - 17, ty + 18, 1, 1, COLOR(255, 160, 160));
    FillRect(cx + 15, ty + 18, 2, 2, grnL);
    FillRect(cx + 15, ty + 18, 1, 1, COLOR(160, 255, 160));
  }

  /* === Layer 3: Canard foreplanes (forward mini-wings) === */
  FillRect(cx - 8, ty + 6, 4, 5, COLOR(25, 100, 200));
  FillRect(cx + 4, ty + 6, 4, 5, COLOR(25, 100, 200));
  /* Canard highlight */
  FillRect(cx - 7, ty + 7, 2, 3, COLOR(50, 150, 240));
  FillRect(cx + 5, ty + 7, 2, 3, COLOR(50, 150, 240));
  /* Canard leading edge */
  FillRect(cx - 8, ty + 6, 3, 1, COLOR(100, 200, 255));
  FillRect(cx + 5, ty + 6, 3, 1, COLOR(100, 200, 255));

  /* === Layer 4: Weapon pods on wings === */
  if (wl >= 1) {
    /* Inner weapon pylons */
    FillRect(cx - 13, ty + 22, 2, 9, COLOR(90, 100, 120));
    FillRect(cx + 11, ty + 22, 2, 9, COLOR(90, 100, 120));
    FillRect(cx - 13, ty + 22, 2, 1, COLOR(150, 170, 190));
    FillRect(cx + 11, ty + 22, 2, 1, COLOR(150, 170, 190));
    FillRect(cx - 13, ty + 30, 2, 1, COLOR(60, 70, 80));
    FillRect(cx + 11, ty + 30, 2, 1, COLOR(60, 70, 80));
    /* Pylon rail detail */
    FillRect(cx - 12, ty + 24, 1, 4, COLOR(110, 120, 140));
    FillRect(cx + 12, ty + 24, 1, 4, COLOR(110, 120, 140));
  }
  if (wl >= 2) {
    /* Mid-wing pylons */
    FillRect(cx - 10, ty + 25, 2, 6, COLOR(80, 90, 110));
    FillRect(cx + 8, ty + 25, 2, 6, COLOR(80, 90, 110));
    FillRect(cx - 10, ty + 25, 2, 1, COLOR(140, 160, 180));
    FillRect(cx + 8, ty + 25, 2, 1, COLOR(140, 160, 180));
  }
  if (wl >= 3) {
    /* Outer wing pylons */
    FillRect(cx - 19, ty + 21, 2, 8, COLOR(70, 80, 100));
    FillRect(cx + 17, ty + 21, 2, 8, COLOR(70, 80, 100));
    /* Pod tips glow */
    FillRect(cx - 13, ty + 22, 2, 2, COLOR(80, 200, 255));
    FillRect(cx + 11, ty + 22, 2, 2, COLOR(80, 200, 255));
    FillRect(cx - 19, ty + 21, 2, 2, COLOR(80, 200, 255));
    FillRect(cx + 17, ty + 21, 2, 2, COLOR(80, 200, 255));
  }
  if (wl >= 4) {
    /* Max level: energy conduits along wings + pod cores */
    FillRect(cx - 16, ty + 19, 6, 1, COLOR(100, 220, 255));
    FillRect(cx + 10, ty + 19, 6, 1, COLOR(100, 220, 255));
    FillRect(cx - 14, ty + 23, 4, 1, COLOR(100, 220, 255));
    FillRect(cx + 10, ty + 23, 4, 1, COLOR(100, 220, 255));
    /* Wing energy rail */
    FillRect(cx - 17, ty + 21, 7, 1, COLOR(120, 240, 255));
    FillRect(cx + 10, ty + 21, 7, 1, COLOR(120, 240, 255));
    /* Pod energy cores */
    FillRect(cx - 13, ty + 24, 2, 2, COLOR(255, 255, 200));
    FillRect(cx + 11, ty + 24, 2, 2, COLOR(255, 255, 200));
    FillRect(cx - 19, ty + 23, 2, 2, COLOR(255, 255, 200));
    FillRect(cx + 17, ty + 23, 2, 2, COLOR(255, 255, 200));
  }

  /* === Layer 5: Central fuselage - expanded === */
  /* Shadow beneath fuselage */
  FillRect(cx - 5, ty + 4, 10, 30, COLOR(8, 40, 100));
  /* Main body with gradient */
  FillRectGradV(cx - 5, ty + 3, 9, 32, COLOR(100, 225, 255), COLOR(22, 85, 165));
  /* Fuselage spine highlight */
  FillRect(cx - 1, ty + 4, 2, 30, COLOR(195, 248, 255));
  /* Body side edge highlights */
  FillRect(cx - 5, ty + 3, 1, 32, COLOR(140, 230, 255));
  FillRect(cx + 3, ty + 3, 1, 32, COLOR(55, 135, 195));
  /* Air intake ramps */
  FillRect(cx - 6, ty + 14, 2, 8, COLOR(15, 70, 150));
  FillRect(cx + 4, ty + 14, 2, 8, COLOR(15, 70, 150));
  FillRect(cx - 5, ty + 15, 1, 6, COLOR(30, 100, 180));
  FillRect(cx + 4, ty + 15, 1, 6, COLOR(30, 100, 180));
  /* Panel lines across body */
  FillRect(cx - 4, ty + 18, 7, 1, COLOR(12, 55, 125));
  FillRect(cx - 4, ty + 24, 7, 1, COLOR(12, 55, 125));
  FillRect(cx - 4, ty + 30, 7, 1, COLOR(12, 55, 125));
  /* Hull marking stripes */
  FillRect(cx - 4, ty + 10, 1, 2, COLOR(255, 200, 60));
  FillRect(cx + 3, ty + 10, 1, 2, COLOR(255, 200, 60));

  /* === Layer 6: Nose cone - sharper === */
  FillRect(cx, ty, 1, 1, COLOR(255, 255, 255));
  FillRect(cx - 1, ty + 1, 2, 1, COLOR(225, 250, 255));
  FillRect(cx - 2, ty + 2, 4, 1, COLOR(175, 238, 255));
  FillRect(cx - 3, ty + 3, 2, 1, COLOR(130, 215, 255));
  FillRect(cx + 1, ty + 3, 2, 1, COLOR(130, 215, 255));
  /* Nose sensor pitot probe */
  FillRect(cx, ty - 2, 1, 2, COLOR(200, 200, 200));

  /* === Layer 7: Cockpit canopy - enhanced === */
  /* Canopy frame thick */
  FillRect(cx - 3, ty + 7, 6, 1, COLOR(12, 55, 130));
  FillRect(cx - 3, ty + 14, 6, 1, COLOR(12, 55, 130));
  FillRect(cx, ty + 7, 1, 8, COLOR(12, 55, 130));
  /* Canopy glass - multi-tone */
  FillRect(cx - 2, ty + 8, 4, 6, COLOR(140, 220, 255));
  FillRect(cx - 1, ty + 9, 2, 3, COLOR(210, 248, 255));
  /* Canopy internal structure */
  FillRect(cx - 1, ty + 11, 2, 1, COLOR(100, 180, 220));
  /* Cockpit reflection glints */
  FillRect(cx - 1, ty + 8, 1, 1, COLOR(255, 255, 255));
  FillRect(cx + 1, ty + 10, 1, 1, COLOR(255, 255, 255));
  /* Instrument panel glow */
  FillRect(cx - 1, ty + 13, 2, 1, COLOR(80, 200, 180));
  /* HUD projector dot */
  FillRect(cx, ty + 8, 1, 1, COLOR(100, 255, 200));

  /* === Layer 8: Tail stabilizers - extended === */
  /* Left stabilizer */
  FillRectGradV(cx - 9, ty + 29, 5, 6, COLOR(38, 125, 225), COLOR(10, 45, 120));
  FillRect(cx - 9, ty + 29, 5, 1, COLOR(85, 195, 255));
  FillRect(cx - 9, ty + 29, 1, 6, COLOR(65, 165, 245));
  /* Right stabilizer */
  FillRectGradV(cx + 4, ty + 29, 5, 6, COLOR(38, 125, 225), COLOR(10, 45, 120));
  FillRect(cx + 4, ty + 29, 5, 1, COLOR(85, 195, 255));
  FillRect(cx + 8, ty + 29, 1, 6, COLOR(65, 165, 245));
  /* Rudder actuators */
  FillRect(cx - 7, ty + 31, 1, 3, COLOR(18, 75, 165));
  FillRect(cx + 6, ty + 31, 1, 3, COLOR(18, 75, 165));
  /* Tail light */
  FillRect(cx, ty + 34, 1, 1, COLOR(255, 255, 255));

  /* === Layer 9: Engine nozzles - detailed === */
  /* Nozzle outer housing */
  FillRect(cx - 6, ty + 33, 5, 3, COLOR(50, 55, 75));
  FillRect(cx + 1, ty + 33, 5, 3, COLOR(50, 55, 75));
  /* Nozzle inner ring segments */
  FillRect(cx - 5, ty + 33, 3, 1, COLOR(180, 140, 80));
  FillRect(cx + 2, ty + 33, 3, 1, COLOR(180, 140, 80));
  /* Nozzle gimbal ring */
  FillRect(cx - 5, ty + 35, 3, 1, COLOR(100, 100, 110));
  FillRect(cx + 2, ty + 35, 3, 1, COLOR(100, 100, 110));
  /* Nozzle heat glow */
  FillRect(cx - 4, ty + 34, 2, 2, COLOR(255, 120, 30));
  FillRect(cx + 2, ty + 34, 2, 2, COLOR(255, 120, 30));

  /* === Muzzle flash effects - expanded === */
  if (p->fireTimer > p->fireRate - 2) {
    FillRect(cx - 2, ty - 6, 4, 7, COLOR(255, 255, 220));
    FillRect(cx - 1, ty - 9, 2, 5, COLOR(255, 255, 180));
    FillRect(cx - 4, ty - 4, 8, 3, COLOR(255, 255, 150));
    /* Side flash bloom */
    FillRect(cx - 5, ty - 3, 1, 2, COLOR(255, 255, 200));
    FillRect(cx + 4, ty - 3, 1, 2, COLOR(255, 255, 200));
    if (wl >= 1) {
      FillRect(cx - 13, ty + 20, 2, 4, COLOR(255, 255, 200));
      FillRect(cx + 11, ty + 20, 2, 4, COLOR(255, 255, 200));
    }
    if (wl >= 3) {
      FillRect(cx - 19, ty + 19, 2, 4, COLOR(255, 255, 200));
      FillRect(cx + 17, ty + 19, 2, 4, COLOR(255, 255, 200));
    }
  }

  /* === Shield effect overlay === */
  if (p->shieldTimer > 0) {
    INT32 sa = p->shieldTimer;
    UINT32 sc1 = COLOR(60, 180, 255);
    UINT32 sc2 = COLOR(30, 100, 200);
    if (sa < SHIELD_DURATION / 4 && (af & 2)) {
      sc1 = COLOR(20, 60, 140);
      sc2 = COLOR(10, 30, 80);
    }
    FillRect(p->x - 6, ty - 6, PLAYER_W + 12, PLAYER_H + 12, sc2);
    FillRect(p->x - 4, ty - 4, PLAYER_W + 8, PLAYER_H + 8, sc1);
    /* Shield hex pattern dots */
    if (af & 1) {
      FillRect(p->x - 2, ty - 4, 2, 2, COLOR(140, 220, 255));
      FillRect(p->x + PLAYER_W, ty - 4, 2, 2, COLOR(140, 220, 255));
      FillRect(p->x - 4, ty + PLAYER_H / 2, 2, 2, COLOR(140, 220, 255));
      FillRect(p->x + PLAYER_W + 2, ty + PLAYER_H / 2, 2, 2, COLOR(140, 220, 255));
    }
    /* Shield border frame */
    FillRect(p->x - 7, ty - 7, PLAYER_W + 14, 1, sc1);
    FillRect(p->x - 7, ty + PLAYER_H + 6, PLAYER_W + 14, 1, sc1);
    FillRect(p->x - 7, ty - 7, 1, PLAYER_H + 14, sc1);
    FillRect(p->x + PLAYER_W + 6, ty - 7, 1, PLAYER_H + 14, sc1);
    /* Corner glints */
    FillRect(p->x - 7, ty - 7, 3, 3, COLOR(180, 240, 255));
    FillRect(p->x + PLAYER_W + 4, ty - 7, 3, 3, COLOR(180, 240, 255));
    FillRect(p->x - 7, ty + PLAYER_H + 4, 3, 3, COLOR(180, 240, 255));
    FillRect(p->x + PLAYER_W + 4, ty + PLAYER_H + 4, 3, 3, COLOR(180, 240, 255));
  }

  /* Blink if invincible */
  if (p->invTimer > 0 && (p->invTimer & 4)) {
    FillRect(p->x, ty, PLAYER_W, PLAYER_H, COLOR(255, 255, 255));
  }
}

/* --- Wingman: small escort fighter --- */
static VOID DrawWingman(Wingman *wm, INT32 animFrame)
{
  INT32 cx = wm->x + WINGMAN_W / 2;
  INT32 by = wm->y + WINGMAN_H;
  INT32 ty = wm->y;

  /* Engine exhaust */
  {
    INT32 fl = 4 + (animFrame % 3) * 2;
    FillRectGradV(cx - 2, by, 2, fl, COLOR(255, 200, 80), COLOR(200, 30, 0));
    FillRectGradV(cx + 1, by, 2, fl, COLOR(255, 200, 80), COLOR(200, 30, 0));
    FillRect(cx - 1, by, 1, fl - 2, COLOR(255, 255, 200));
    FillRect(cx + 1, by, 1, fl - 2, COLOR(255, 255, 200));
  }

  /* Wings - small delta */
  FillRect(cx - 7, ty + 7, 3, 8, COLOR(25, 95, 190));
  FillRect(cx + 4, ty + 7, 3, 8, COLOR(25, 95, 190));
  FillRect(cx - 6, ty + 8, 2, 5, COLOR(45, 150, 240));
  FillRect(cx + 4, ty + 8, 2, 5, COLOR(45, 150, 240));
  /* Wing leading edge */
  FillRect(cx - 7, ty + 7, 2, 1, COLOR(100, 200, 255));
  FillRect(cx + 5, ty + 7, 2, 1, COLOR(100, 200, 255));

  /* Body */
  FillRectGradV(cx - 2, ty + 2, 5, 14, COLOR(80, 200, 255), COLOR(20, 80, 160));
  FillRect(cx, ty + 3, 1, 12, COLOR(170, 240, 255));
  /* Body side edges */
  FillRect(cx - 2, ty + 2, 1, 14, COLOR(120, 220, 255));
  FillRect(cx + 2, ty + 2, 1, 14, COLOR(40, 120, 180));

  /* Nose */
  FillRect(cx, ty, 1, 1, COLOR(255, 255, 255));
  FillRect(cx - 1, ty + 1, 3, 1, COLOR(200, 240, 255));

  /* Cockpit */
  FillRect(cx - 1, ty + 4, 3, 3, COLOR(140, 220, 255));
  FillRect(cx, ty + 5, 1, 1, COLOR(220, 250, 255));

  /* Tail fins */
  FillRect(cx - 4, ty + 14, 2, 3, COLOR(25, 100, 200));
  FillRect(cx + 2, ty + 14, 2, 3, COLOR(25, 100, 200));
  FillRect(cx - 4, ty + 14, 1, 3, COLOR(50, 150, 240));
  FillRect(cx + 3, ty + 14, 1, 3, COLOR(50, 150, 240));

  /* Navigation light */
  if (wm->side < 0) {
    FillRect(cx - 7, ty + 7, 1, 1, COLOR(255, 80, 80));
  } else {
    FillRect(cx + 6, ty + 7, 1, 1, COLOR(80, 255, 80));
  }
}

/* --- Small enemy: razor interceptor - enhanced --- */
static VOID DrawEnemySmall(Enemy *e)
{
  INT32 cx = e->x + e->w / 2;
  INT32 ey = e->y;
  INT32 mt = e->moveTimer;

  /* Animated engine exhaust */
  {
    INT32 fl = 4 + (mt % 3) * 2;
    FillRectGradV(cx - 3, ey + 18, 6, fl, COLOR(255, 160, 30), COLOR(180, 20, 0));
    FillRect(cx - 2, ey + 18, 4, fl - 2, COLOR(255, 220, 80));
    FillRect(cx - 1, ey + 18, 2, fl - 3, COLOR(255, 255, 200));
  }

  /* Swept-back dagger wings - gradient */
  FillRectGradV(cx - 12, ey + 9, 6, 7, COLOR(210, 55, 28), COLOR(130, 18, 8));
  FillRectGradV(cx + 6, ey + 9, 6, 7, COLOR(210, 55, 28), COLOR(130, 18, 8));
  /* Wing upper highlight stripe */
  FillRect(cx - 11, ey + 9, 4, 2, COLOR(245, 95, 55));
  FillRect(cx + 7, ey + 9, 4, 2, COLOR(245, 95, 55));
  /* Wing leading edge bright */
  FillRect(cx - 12, ey + 9, 5, 1, COLOR(255, 140, 90));
  FillRect(cx + 7, ey + 9, 5, 1, COLOR(255, 140, 90));
  /* Wing trailing edge dark */
  FillRect(cx - 12, ey + 15, 6, 1, COLOR(100, 15, 8));
  FillRect(cx + 6, ey + 15, 6, 1, COLOR(100, 15, 8));
  /* Wing tip extensions */
  FillRect(cx - 11, ey + 15, 3, 4, COLOR(140, 25, 12));
  FillRect(cx + 8, ey + 15, 3, 4, COLOR(140, 25, 12));
  /* Wing tip weapon stub */
  FillRect(cx - 12, ey + 16, 1, 3, COLOR(80, 80, 90));
  FillRect(cx + 11, ey + 16, 1, 3, COLOR(80, 80, 90));
  /* Wing tip glow - animated */
  {
    UINT32 tipC = (mt & 4) ? COLOR(255, 60, 30) : COLOR(255, 120, 80);
    FillRect(cx - 12, ey + 16, 1, 1, tipC);
    FillRect(cx + 11, ey + 16, 1, 1, tipC);
  }
  /* Wing panel line */
  FillRect(cx - 11, ey + 12, 4, 1, COLOR(120, 20, 10));
  FillRect(cx + 7, ey + 12, 4, 1, COLOR(120, 20, 10));

  /* Central body - gradient */
  FillRectGradV(cx - 5, ey + 2, 10, 16, COLOR(235, 70, 40), COLOR(140, 18, 10));
  /* Body center spine highlight */
  FillRect(cx - 1, ey + 3, 2, 14, COLOR(255, 115, 70));
  /* Body side edges */
  FillRect(cx - 5, ey + 2, 1, 16, COLOR(200, 55, 30));
  FillRect(cx + 4, ey + 2, 1, 16, COLOR(90, 12, 6));
  /* Body panel lines */
  FillRect(cx - 4, ey + 10, 3, 1, COLOR(100, 15, 8));
  FillRect(cx + 1, ey + 10, 3, 1, COLOR(100, 15, 8));
  FillRect(cx - 4, ey + 14, 3, 1, COLOR(100, 15, 8));
  FillRect(cx + 1, ey + 14, 3, 1, COLOR(100, 15, 8));
  /* Body ventral ridge */
  FillRect(cx, ey + 4, 1, 12, COLOR(180, 45, 25));

  /* Nose - sharp multi-layer */
  FillRect(cx, ey - 1, 1, 1, COLOR(255, 240, 200));
  FillRect(cx - 1, ey, 2, 1, COLOR(255, 220, 180));
  FillRect(cx - 2, ey + 1, 4, 1, COLOR(255, 150, 100));
  FillRect(cx - 3, ey + 2, 2, 1, COLOR(240, 100, 60));
  FillRect(cx + 1, ey + 2, 2, 1, COLOR(240, 100, 60));

  /* Eye/visor - menacing glow with animation */
  {
    UINT32 eyeC = (mt & 2) ? COLOR(255, 255, 80) : COLOR(255, 240, 50);
    FillRect(cx - 4, ey + 6, 8, 3, eyeC);
    FillRect(cx - 3, ey + 6, 6, 1, COLOR(255, 255, 180));
    FillRect(cx - 1, ey + 7, 2, 1, COLOR(255, 255, 240));
  }
  /* Eye socket shadows */
  FillRect(cx - 5, ey + 6, 1, 3, COLOR(70, 10, 4));
  FillRect(cx + 4, ey + 6, 1, 3, COLOR(70, 10, 4));
  /* Eye brow ridge */
  FillRect(cx - 4, ey + 5, 8, 1, COLOR(100, 15, 6));

  /* Tail fins - detailed */
  FillRect(cx - 5, ey + 17, 3, 4, COLOR(140, 25, 12));
  FillRect(cx + 2, ey + 17, 3, 4, COLOR(140, 25, 12));
  FillRect(cx - 5, ey + 17, 1, 4, COLOR(190, 45, 22));
  FillRect(cx + 4, ey + 17, 1, 4, COLOR(190, 45, 22));
  /* Tail fin tips */
  FillRect(cx - 5, ey + 17, 1, 1, COLOR(230, 70, 35));
  FillRect(cx + 4, ey + 17, 1, 1, COLOR(230, 70, 35));
}

/* --- Medium enemy: strike bomber - enhanced --- */
static VOID DrawEnemyMedium(Enemy *e)
{
  INT32 cx = e->x + e->w / 2;
  INT32 ey = e->y;
  INT32 mt = e->moveTimer;

  /* Animated engine exhaust */
  {
    INT32 fl = 4 + (mt % 3) * 2;
    FillRectGradV(cx - 4, ey + 26, 3, fl, COLOR(255, 160, 30), COLOR(180, 15, 0));
    FillRectGradV(cx + 1, ey + 26, 3, fl, COLOR(255, 160, 30), COLOR(180, 15, 0));
    FillRect(cx - 3, ey + 26, 1, fl - 2, COLOR(255, 255, 150));
    FillRect(cx + 2, ey + 26, 1, fl - 2, COLOR(255, 255, 150));
  }

  /* Broad attack wings - gradient */
  FillRectGradV(cx - 18, ey + 8, 9, 14, COLOR(175, 38, 22), COLOR(110, 15, 8));
  FillRectGradV(cx + 9, ey + 8, 9, 14, COLOR(175, 38, 22), COLOR(110, 15, 8));
  /* Wing upper surface highlight */
  FillRect(cx - 16, ey + 7, 5, 5, COLOR(215, 65, 38));
  FillRect(cx + 11, ey + 7, 5, 5, COLOR(215, 65, 38));
  /* Wing leading edge bright */
  FillRect(cx - 18, ey + 8, 8, 1, COLOR(255, 120, 70));
  FillRect(cx + 10, ey + 8, 8, 1, COLOR(255, 120, 70));
  /* Wing trailing edge shadow */
  FillRect(cx - 18, ey + 21, 9, 1, COLOR(80, 10, 5));
  FillRect(cx + 9, ey + 21, 9, 1, COLOR(80, 10, 5));
  /* Wing panel lines */
  FillRect(cx - 16, ey + 13, 5, 1, COLOR(100, 18, 10));
  FillRect(cx + 11, ey + 13, 5, 1, COLOR(100, 18, 10));
  FillRect(cx - 15, ey + 17, 4, 1, COLOR(100, 18, 10));
  FillRect(cx + 11, ey + 17, 4, 1, COLOR(100, 18, 10));
  /* Wing root fairing */
  FillRect(cx - 9, ey + 9, 2, 12, COLOR(135, 28, 15));
  FillRect(cx + 7, ey + 9, 2, 12, COLOR(135, 28, 15));
  /* Wing root highlight */
  FillRect(cx - 8, ey + 10, 1, 10, COLOR(180, 45, 25));
  FillRect(cx + 7, ey + 10, 1, 10, COLOR(180, 45, 25));

  /* Wing weapon pods - enhanced with animated glow */
  FillRect(cx - 17, ey + 14, 5, 8, COLOR(75, 12, 6));
  FillRect(cx + 12, ey + 14, 5, 8, COLOR(75, 12, 6));
  /* Pod cap */
  FillRect(cx - 17, ey + 14, 5, 1, COLOR(155, 42, 24));
  FillRect(cx + 12, ey + 14, 5, 1, COLOR(155, 42, 24));
  /* Pod base shadow */
  FillRect(cx - 17, ey + 21, 5, 1, COLOR(40, 8, 4));
  FillRect(cx + 12, ey + 21, 5, 1, COLOR(40, 8, 4));
  /* Pod barrel housing */
  FillRect(cx - 16, ey + 12, 3, 3, COLOR(50, 50, 60));
  FillRect(cx + 13, ey + 12, 3, 3, COLOR(50, 50, 60));
  /* Pod barrel bore */
  FillRect(cx - 15, ey + 12, 1, 1, COLOR(30, 30, 35));
  FillRect(cx + 14, ey + 12, 1, 1, COLOR(30, 30, 35));
  /* Pod muzzle glow - animated */
  {
    UINT32 muzC = (mt & 3) ? COLOR(255, 80, 30) : COLOR(255, 150, 80);
    FillRect(cx - 15, ey + 12, 1, 1, muzC);
    FillRect(cx + 14, ey + 12, 1, 1, muzC);
  }
  /* Pod side rail */
  FillRect(cx - 17, ey + 16, 1, 5, COLOR(90, 90, 100));
  FillRect(cx + 16, ey + 16, 1, 5, COLOR(90, 90, 100));

  /* Central body - gradient */
  FillRectGradV(cx - 7, ey + 2, 14, 23, COLOR(240, 78, 45), COLOR(140, 18, 8));
  /* Body spine */
  FillRect(cx - 1, ey + 3, 2, 21, COLOR(255, 105, 58));
  /* Body side edges */
  FillRect(cx - 7, ey + 2, 1, 23, COLOR(195, 52, 30));
  FillRect(cx + 6, ey + 2, 1, 23, COLOR(85, 10, 5));
  /* Body ventral keel */
  FillRect(cx, ey + 5, 1, 18, COLOR(170, 40, 22));

  /* Armored nose - layered plates */
  FillRect(cx - 4, ey, 8, 5, COLOR(195, 52, 28));
  FillRect(cx - 3, ey + 1, 6, 3, COLOR(235, 85, 50));
  FillRect(cx - 2, ey + 1, 4, 2, COLOR(250, 120, 70));
  FillRect(cx - 1, ey, 2, 1, COLOR(255, 210, 160));
  /* Nose sensor probe */
  FillRect(cx, ey - 1, 1, 1, COLOR(200, 200, 210));

  /* Twin eyes - menacing with animation */
  {
    UINT32 eyeC = (mt & 4) ? COLOR(255, 255, 80) : COLOR(255, 240, 50);
    FillRect(cx - 5, ey + 6, 3, 4, eyeC);
    FillRect(cx + 2, ey + 6, 3, 4, eyeC);
  }
  /* Eye pupils */
  FillRect(cx - 4, ey + 7, 1, 2, COLOR(255, 255, 200));
  FillRect(cx + 3, ey + 7, 1, 2, COLOR(255, 255, 200));
  /* Eye socket shadows */
  FillRect(cx - 6, ey + 6, 1, 4, COLOR(65, 8, 4));
  FillRect(cx + 5, ey + 6, 1, 4, COLOR(65, 8, 4));
  /* Eye brow ridge */
  FillRect(cx - 5, ey + 5, 10, 1, COLOR(100, 15, 6));

  /* Center cockpit detail */
  FillRect(cx - 2, ey + 12, 4, 4, COLOR(115, 16, 6));
  FillRect(cx - 1, ey + 13, 2, 2, COLOR(165, 38, 20));
  /* Cockpit glass reflection */
  FillRect(cx - 1, ey + 12, 2, 1, COLOR(185, 85, 55));
  FillRect(cx, ey + 13, 1, 1, COLOR(200, 120, 80));

  /* Body panel lines */
  FillRect(cx - 6, ey + 18, 12, 1, COLOR(80, 12, 6));
  FillRect(cx - 5, ey + 22, 10, 1, COLOR(80, 12, 6));

  /* Tail section - expanded */
  FillRect(cx - 6, ey + 23, 12, 5, COLOR(115, 16, 6));
  /* Tail stabilizers */
  FillRect(cx - 8, ey + 23, 3, 4, COLOR(145, 28, 14));
  FillRect(cx + 5, ey + 23, 3, 4, COLOR(145, 28, 14));
  /* Tail edge highlight */
  FillRect(cx - 8, ey + 23, 1, 4, COLOR(185, 42, 22));
  FillRect(cx + 7, ey + 23, 1, 4, COLOR(185, 42, 22));
  /* Tail center light */
  FillRect(cx, ey + 24, 1, 1, COLOR(255, 60, 60));
}

/* --- Large enemy: armored gunship with heavy weapons --- */
static VOID DrawEnemyLarge(Enemy *e)
{
  INT32 cx = e->x + e->w / 2;
  INT32 ey = e->y;
  INT32 mt = e->moveTimer;

  /* Animated engine exhaust trails */
  {
    INT32 fl = 6 + (mt % 4) * 2;
    FillRectGradV(cx - 6, ey + 38, 4, fl, COLOR(255, 160, 30), COLOR(140, 10, 0));
    FillRectGradV(cx + 2, ey + 38, 4, fl, COLOR(255, 160, 30), COLOR(140, 10, 0));
    FillRect(cx - 5, ey + 38, 2, fl - 2, COLOR(255, 230, 100));
    FillRect(cx + 3, ey + 38, 2, fl - 2, COLOR(255, 230, 100));
    /* Side thruster trails */
    FillRectGradV(cx - 22, ey + 36, 2, fl - 3, COLOR(200, 100, 20), COLOR(100, 10, 0));
    FillRectGradV(cx + 20, ey + 36, 2, fl - 3, COLOR(200, 100, 20), COLOR(100, 10, 0));
  }

  /* Heavy armored wings - multi-layer gradient */
  FillRectGradV(cx - 24, ey + 10, 12, 28, COLOR(155, 25, 18), COLOR(70, 8, 4));
  FillRectGradV(cx + 12, ey + 10, 12, 28, COLOR(155, 25, 18), COLOR(70, 8, 4));
  /* Wing upper fairing with highlight */
  FillRect(cx - 22, ey + 6, 8, 7, COLOR(110, 32, 20));
  FillRect(cx + 14, ey + 6, 8, 7, COLOR(110, 32, 20));
  FillRect(cx - 21, ey + 6, 6, 1, COLOR(185, 58, 32));
  FillRect(cx + 15, ey + 6, 6, 1, COLOR(185, 58, 32));
  /* Wing leading edge */
  FillRect(cx - 24, ey + 10, 11, 1, COLOR(215, 68, 38));
  FillRect(cx + 13, ey + 10, 11, 1, COLOR(215, 68, 38));
  /* Wing trailing edge shadow */
  FillRect(cx - 24, ey + 37, 12, 1, COLOR(40, 6, 3));
  FillRect(cx + 12, ey + 37, 12, 1, COLOR(40, 6, 3));
  /* Wing panel lines */
  FillRect(cx - 20, ey + 18, 6, 1, COLOR(65, 8, 5));
  FillRect(cx + 14, ey + 18, 6, 1, COLOR(65, 8, 5));
  FillRect(cx - 20, ey + 26, 6, 1, COLOR(65, 8, 5));
  FillRect(cx + 14, ey + 26, 6, 1, COLOR(65, 8, 5));
  FillRect(cx - 22, ey + 22, 8, 1, COLOR(65, 8, 5));
  FillRect(cx + 14, ey + 22, 8, 1, COLOR(65, 8, 5));
  /* Wing root strake */
  FillRect(cx - 11, ey + 10, 2, 26, COLOR(125, 24, 14));
  FillRect(cx + 9, ey + 10, 2, 26, COLOR(125, 24, 14));
  /* Wing underside shadow */
  FillRect(cx - 23, ey + 35, 10, 2, COLOR(55, 8, 5));
  FillRect(cx + 13, ey + 35, 10, 2, COLOR(55, 8, 5));
  /* Wing tip lights */
  FillRect(cx - 24, ey + 10, 1, 2, COLOR(255, 80, 40));
  FillRect(cx + 23, ey + 10, 1, 2, COLOR(255, 80, 40));

  /* Wing weapon turret mounts - enhanced with animation */
  FillRect(cx - 22, ey + 14, 7, 9, COLOR(60, 60, 72));
  FillRect(cx + 15, ey + 14, 7, 9, COLOR(60, 60, 72));
  /* Turret cap highlight */
  FillRect(cx - 21, ey + 14, 5, 1, COLOR(135, 135, 155));
  FillRect(cx + 16, ey + 14, 5, 1, COLOR(135, 135, 155));
  /* Turret base shadow */
  FillRect(cx - 22, ey + 22, 7, 1, COLOR(35, 35, 45));
  FillRect(cx + 15, ey + 22, 7, 1, COLOR(35, 35, 45));
  /* Turret barrel housing */
  FillRect(cx - 21, ey + 22, 5, 5, COLOR(95, 38, 24));
  FillRect(cx + 16, ey + 22, 5, 5, COLOR(95, 38, 24));
  /* Turret barrels */
  FillRect(cx - 20, ey + 10, 3, 5, COLOR(42, 42, 52));
  FillRect(cx + 17, ey + 10, 3, 5, COLOR(42, 42, 52));
  /* Barrel bore */
  FillRect(cx - 19, ey + 10, 1, 1, COLOR(25, 25, 30));
  FillRect(cx + 18, ey + 10, 1, 1, COLOR(25, 25, 30));
  /* Barrel muzzle flash - animated */
  {
    UINT32 muzC = (mt & 3) ? COLOR(255, 120, 40) : COLOR(255, 200, 100);
    FillRect(cx - 20, ey + 10, 3, 1, muzC);
    FillRect(cx + 17, ey + 10, 3, 1, muzC);
  }
  /* Turret side armor */
  FillRect(cx - 22, ey + 15, 1, 7, COLOR(80, 80, 92));
  FillRect(cx + 21, ey + 15, 1, 7, COLOR(80, 80, 92));

  /* Central body - heavily armored gradient */
  FillRectGradV(cx - 10, ey + 2, 20, 36, COLOR(210, 55, 30), COLOR(110, 15, 8));
  /* Body spine highlight */
  FillRect(cx - 1, ey + 3, 2, 34, COLOR(235, 92, 52));
  /* Body edge contrast */
  FillRect(cx - 10, ey + 2, 1, 36, COLOR(185, 48, 28));
  FillRect(cx + 9, ey + 2, 1, 36, COLOR(65, 8, 4));
  /* Body ventral keel */
  FillRect(cx, ey + 5, 1, 30, COLOR(160, 35, 18));

  /* Nose armor plates - layered detail */
  FillRect(cx - 7, ey, 14, 5, COLOR(170, 45, 28));
  FillRect(cx - 5, ey + 1, 10, 3, COLOR(230, 82, 50));
  FillRect(cx - 3, ey + 1, 6, 2, COLOR(248, 122, 72));
  FillRect(cx - 1, ey, 2, 1, COLOR(255, 195, 145));
  /* Nose tip sensor */
  FillRect(cx, ey - 1, 1, 1, COLOR(255, 255, 255));
  /* Nose chin sensor */
  FillRect(cx - 2, ey + 4, 4, 1, COLOR(140, 40, 22));

  /* Eyes - triple menacing array with animation */
  {
    UINT32 eyeC = (mt & 4) ? COLOR(255, 220, 50) : COLOR(255, 205, 35);
    FillRect(cx - 8, ey + 10, 4, 4, eyeC);
    FillRect(cx + 4, ey + 10, 4, 4, eyeC);
  }
  FillRect(cx - 1, ey + 11, 2, 3, COLOR(255, 235, 110));
  /* Eye pupils */
  FillRect(cx - 7, ey + 11, 2, 2, COLOR(255, 255, 195));
  FillRect(cx + 5, ey + 11, 2, 2, COLOR(255, 255, 195));
  FillRect(cx, ey + 12, 1, 1, COLOR(255, 255, 255));
  /* Eye socket shadows */
  FillRect(cx - 9, ey + 10, 1, 4, COLOR(55, 6, 3));
  FillRect(cx + 8, ey + 10, 1, 4, COLOR(55, 6, 3));
  /* Eye brow ridge */
  FillRect(cx - 8, ey + 9, 16, 1, COLOR(80, 10, 5));

  /* Body panel lines */
  FillRect(cx - 8, ey + 18, 16, 1, COLOR(65, 8, 5));
  FillRect(cx - 6, ey + 24, 12, 1, COLOR(65, 8, 5));
  FillRect(cx - 7, ey + 30, 14, 1, COLOR(65, 8, 5));
  /* Rivet dots on panels */
  FillRect(cx - 7, ey + 19, 1, 1, COLOR(125, 32, 18));
  FillRect(cx + 6, ey + 19, 1, 1, COLOR(125, 32, 18));
  FillRect(cx - 5, ey + 25, 1, 1, COLOR(125, 32, 18));
  FillRect(cx + 4, ey + 25, 1, 1, COLOR(125, 32, 18));
  FillRect(cx - 8, ey + 31, 1, 1, COLOR(125, 32, 18));
  FillRect(cx + 7, ey + 31, 1, 1, COLOR(125, 32, 18));
  /* Hull marking stripe */
  FillRect(cx - 9, ey + 16, 1, 4, COLOR(255, 180, 60));
  FillRect(cx + 8, ey + 16, 1, 4, COLOR(255, 180, 60));

  /* Exhaust vents - enhanced */
  FillRect(cx - 7, ey + 36, 4, 3, COLOR(195, 78, 20));
  FillRect(cx + 3, ey + 36, 4, 3, COLOR(195, 78, 20));
  /* Vent inner glow */
  FillRect(cx - 6, ey + 37, 2, 1, COLOR(255, 210, 65));
  FillRect(cx + 4, ey + 37, 2, 1, COLOR(255, 210, 65));
  /* Vent housing rim */
  FillRect(cx - 7, ey + 36, 4, 1, COLOR(105, 38, 18));
  FillRect(cx + 3, ey + 36, 4, 1, COLOR(105, 38, 18));

  /* HP bar with gradient */
  if (e->hp < e->maxHp) {
    FillRect(e->x, ey - 8, e->w, 6, COLOR(50, 15, 15));
    FillRect(e->x + 1, ey - 7, (e->w - 2) * e->hp / e->maxHp, 4, COLOR(255, 55, 55));
    FillRect(e->x + 1, ey - 7, (e->w - 2) * e->hp / e->maxHp, 2, COLOR(255, 140, 140));
    FillRect(e->x, ey - 8, e->w, 1, COLOR(100, 30, 30));
    FillRect(e->x, ey - 3, e->w, 1, COLOR(35, 8, 8));
  }
}

/* --- Boss: massive dreadnought with full detail + animations --- */
static VOID DrawEnemyBoss(Enemy *e)
{
  INT32 cx = e->x + e->w / 2;
  INT32 ey = e->y;
  INT32 mt = e->moveTimer;

  /* === Animated engine exhaust array === */
  {
    INT32 fl = 8 + (mt % 4) * 3;
    FillRectGradV(cx - 22, ey + 52, 6, fl, COLOR(255, 100, 180), COLOR(120, 0, 60));
    FillRectGradV(cx + 16, ey + 52, 6, fl, COLOR(255, 100, 180), COLOR(120, 0, 60));
    FillRectGradV(cx - 6, ey + 54, 12, fl + 2, COLOR(255, 140, 220), COLOR(160, 0, 80));
    /* Exhaust core glow */
    FillRect(cx - 20, ey + 52, 3, fl - 2, COLOR(255, 200, 240));
    FillRect(cx + 18, ey + 52, 3, fl - 2, COLOR(255, 200, 240));
    FillRect(cx - 4, ey + 54, 8, fl, COLOR(255, 220, 255));
    /* Exhaust sparkle */
    if (mt & 1) {
      FillRect(cx - 19, ey + 52 + fl, 1, 1, COLOR(255, 255, 255));
      FillRect(cx + 19, ey + 52 + fl, 1, 1, COLOR(255, 255, 255));
    }
  }

  /* === Main hull - massive multi-layer structure === */
  FillRectGradV(cx - 36, ey + 14, 72, 40, COLOR(170, 35, 170), COLOR(80, 8, 80));
  /* Hull upper highlight band */
  FillRect(cx - 34, ey + 14, 68, 2, COLOR(220, 80, 220));
  /* Hull lower shadow band */
  FillRect(cx - 34, ey + 51, 68, 2, COLOR(50, 5, 50));
  /* Hull panel segments */
  FillRect(cx - 30, ey + 22, 60, 1, COLOR(60, 8, 60));
  FillRect(cx - 30, ey + 30, 60, 1, COLOR(60, 8, 60));
  FillRect(cx - 30, ey + 38, 60, 1, COLOR(60, 8, 60));
  FillRect(cx - 30, ey + 44, 60, 1, COLOR(60, 8, 60));
  /* Hull vertical ribs */
  FillRect(cx - 20, ey + 16, 1, 36, COLOR(72, 10, 72));
  FillRect(cx - 10, ey + 16, 1, 36, COLOR(72, 10, 72));
  FillRect(cx + 9, ey + 16, 1, 36, COLOR(72, 10, 72));
  FillRect(cx + 19, ey + 16, 1, 36, COLOR(72, 10, 72));
  /* Hull edge lights - animated */
  {
    UINT32 lc = (mt & 4) ? COLOR(100, 180, 255) : COLOR(80, 150, 255);
    FillRect(cx - 36, ey + 20, 1, 3, lc);
    FillRect(cx - 36, ey + 30, 1, 3, lc);
    FillRect(cx - 36, ey + 40, 1, 3, lc);
    FillRect(cx + 35, ey + 20, 1, 3, lc);
    FillRect(cx + 35, ey + 30, 1, 3, lc);
    FillRect(cx + 35, ey + 40, 1, 3, lc);
  }
  /* Hull rune markings */
  FillRect(cx - 28, ey + 16, 1, 2, COLOR(200, 80, 200));
  FillRect(cx + 27, ey + 16, 1, 2, COLOR(200, 80, 200));
  FillRect(cx - 28, ey + 48, 1, 2, COLOR(100, 30, 100));
  FillRect(cx + 27, ey + 48, 1, 2, COLOR(100, 30, 100));

  /* === Top dome - command center === */
  FillRectGradV(cx - 20, ey + 4, 40, 14, COLOR(190, 50, 190), COLOR(120, 18, 120));
  /* Dome upper arc highlight */
  FillRect(cx - 18, ey + 4, 36, 1, COLOR(235, 105, 235));
  FillRect(cx - 14, ey + 3, 28, 1, COLOR(235, 105, 235));
  /* Dome viewport - layered windows */
  FillRect(cx - 12, ey + 6, 24, 5, COLOR(220, 100, 220));
  FillRect(cx - 10, ey + 7, 20, 3, COLOR(245, 170, 245));
  FillRect(cx - 8, ey + 8, 16, 1, COLOR(255, 215, 255));
  /* Dome window frames */
  FillRect(cx - 12, ey + 6, 24, 1, COLOR(95, 15, 95));
  FillRect(cx - 12, ey + 10, 24, 1, COLOR(95, 15, 95));
  FillRect(cx - 6, ey + 6, 1, 5, COLOR(95, 15, 95));
  FillRect(cx + 5, ey + 6, 1, 5, COLOR(95, 15, 95));
  /* Dome crest ornament */
  FillRect(cx - 2, ey + 3, 4, 1, COLOR(255, 180, 255));

  /* === Core energy reactor - pulsing with animation === */
  {
    INT32 pulse = (mt >> 2) & 3;
    UINT32 coreC = COLOR(255, 150 + pulse * 28, 255);
    UINT32 innerC = COLOR(255, 200 + pulse * 18, 255);
    /* Reactor housing */
    FillRect(cx - 8, ey + 20, 16, 14, COLOR(95, 12, 95));
    /* Reactor core */
    FillRect(cx - 6, ey + 22, 12, 10, coreC);
    FillRect(cx - 3, ey + 24, 6, 6, innerC);
    FillRect(cx - 1, ey + 26, 2, 2, COLOR(255, 255, 255));
    /* Reactor ring */
    FillRect(cx - 7, ey + 21, 14, 1, COLOR(185, 65, 185));
    FillRect(cx - 7, ey + 33, 14, 1, COLOR(75, 8, 75));
    /* Reactor energy tendrils - animated */
    if (pulse & 1) {
      FillRect(cx - 5, ey + 20, 1, 2, COLOR(255, 180, 255));
      FillRect(cx + 4, ey + 20, 1, 2, COLOR(255, 180, 255));
      FillRect(cx - 5, ey + 33, 1, 2, COLOR(255, 120, 255));
      FillRect(cx + 4, ey + 33, 1, 2, COLOR(255, 120, 255));
    }
  }

  /* === Side wings - heavy extensions with gradient === */
  FillRectGradV(cx - 44, ey + 22, 14, 26, COLOR(135, 22, 135), COLOR(65, 8, 65));
  FillRectGradV(cx + 30, ey + 22, 14, 26, COLOR(135, 22, 135), COLOR(65, 8, 65));
  /* Wing armor plates */
  FillRect(cx - 42, ey + 20, 10, 4, COLOR(160, 40, 160));
  FillRect(cx + 32, ey + 20, 10, 4, COLOR(160, 40, 160));
  /* Wing leading edge */
  FillRect(cx - 43, ey + 22, 12, 1, COLOR(185, 60, 185));
  FillRect(cx + 31, ey + 22, 12, 1, COLOR(185, 60, 185));
  /* Wing trailing edge shadow */
  FillRect(cx - 44, ey + 47, 14, 1, COLOR(40, 5, 40));
  FillRect(cx + 30, ey + 47, 14, 1, COLOR(40, 5, 40));
  /* Wing panel lines */
  FillRect(cx - 42, ey + 30, 10, 1, COLOR(55, 6, 55));
  FillRect(cx + 32, ey + 30, 10, 1, COLOR(55, 6, 55));
  FillRect(cx - 42, ey + 38, 10, 1, COLOR(55, 6, 55));
  FillRect(cx + 32, ey + 38, 10, 1, COLOR(55, 6, 55));
  /* Wing underside shadow */
  FillRect(cx - 43, ey + 45, 12, 2, COLOR(50, 5, 50));
  FillRect(cx + 31, ey + 45, 12, 2, COLOR(50, 5, 50));
  /* Wing tip lights - animated */
  {
    UINT32 wtl = (mt & 2) ? COLOR(255, 60, 60) : COLOR(180, 20, 20);
    UINT32 wtr = (mt & 2) ? COLOR(60, 255, 60) : COLOR(20, 180, 20);
    FillRect(cx - 44, ey + 22, 2, 2, wtl);
    FillRect(cx + 42, ey + 22, 2, 2, wtr);
  }

  /* === Weapon turrets - 4 mounts with animated fire === */
  /* Outer turret bases */
  FillRect(cx - 42, ey + 18, 8, 10, COLOR(190, 72, 190));
  FillRect(cx + 34, ey + 18, 8, 10, COLOR(190, 72, 190));
  /* Outer turret caps */
  FillRect(cx - 41, ey + 18, 6, 1, COLOR(230, 115, 230));
  FillRect(cx + 35, ey + 18, 6, 1, COLOR(230, 115, 230));
  /* Inner turret bases */
  FillRect(cx - 24, ey + 16, 7, 9, COLOR(190, 72, 190));
  FillRect(cx + 17, ey + 16, 7, 9, COLOR(190, 72, 190));
  /* Inner turret caps */
  FillRect(cx - 23, ey + 16, 5, 1, COLOR(230, 115, 230));
  FillRect(cx + 18, ey + 16, 5, 1, COLOR(230, 115, 230));
  /* Turret barrels - outer */
  FillRect(cx - 40, ey + 28, 2, 14, COLOR(150, 35, 150));
  FillRect(cx + 38, ey + 28, 2, 14, COLOR(150, 35, 150));
  /* Turret barrels - inner */
  FillRect(cx - 22, ey + 25, 2, 12, COLOR(150, 35, 150));
  FillRect(cx + 20, ey + 25, 2, 12, COLOR(150, 35, 150));
  /* Barrel muzzle flash - animated */
  {
    UINT32 muzC = (mt % 5 < 2) ? COLOR(255, 220, 120) : COLOR(255, 200, 100);
    FillRect(cx - 40, ey + 28, 2, 1, muzC);
    FillRect(cx + 38, ey + 28, 2, 1, muzC);
    FillRect(cx - 22, ey + 25, 2, 1, muzC);
    FillRect(cx + 20, ey + 25, 2, 1, muzC);
  }
  /* Barrel tip glow */
  FillRect(cx - 40, ey + 41, 2, 1, COLOR(255, 100, 40));
  FillRect(cx + 38, ey + 41, 2, 1, COLOR(255, 100, 40));
  FillRect(cx - 22, ey + 36, 2, 1, COLOR(255, 100, 40));
  FillRect(cx + 20, ey + 36, 2, 1, COLOR(255, 100, 40));

  /* === Main eyes - threatening triple with animation === */
  {
    UINT32 eyeC = (mt & 4) ? COLOR(255, 50, 50) : COLOR(255, 35, 35);
    FillRect(cx - 14, ey + 22, 7, 7, eyeC);
    FillRect(cx + 7, ey + 22, 7, 7, eyeC);
  }
  /* Eye inner glow */
  FillRect(cx - 12, ey + 24, 4, 4, COLOR(255, 190, 90));
  FillRect(cx + 9, ey + 24, 4, 4, COLOR(255, 190, 90));
  /* Eye pupil */
  FillRect(cx - 11, ey + 25, 2, 2, COLOR(255, 255, 255));
  FillRect(cx + 10, ey + 25, 2, 2, COLOR(255, 255, 255));
  /* Central eye */
  FillRect(cx - 2, ey + 23, 4, 4, COLOR(255, 65, 65));
  FillRect(cx - 1, ey + 24, 2, 2, COLOR(255, 205, 125));
  /* Eye socket frames */
  FillRect(cx - 15, ey + 21, 9, 1, COLOR(75, 6, 75));
  FillRect(cx - 15, ey + 29, 9, 1, COLOR(75, 6, 75));
  FillRect(cx + 6, ey + 21, 9, 1, COLOR(75, 6, 75));
  FillRect(cx + 6, ey + 29, 9, 1, COLOR(75, 6, 75));
  /* Eye brow ridge */
  FillRect(cx - 14, ey + 20, 28, 1, COLOR(60, 5, 60));

  /* === Animated scanning beam === */
  {
    INT32 scanY = ey + 20 + (mt % 20);
    FillRect(cx - 38, ey + scanY, 76, 1, COLOR(80, 40, 120));
    FillRect(cx - 20, ey + scanY, 40, 1, COLOR(120, 60, 160));
  }

  /* === Shield barrier edges - animated pulse === */
  {
    UINT32 shC1 = (mt & 3) ? COLOR(80, 155, 255) : COLOR(60, 120, 220);
    UINT32 shC2 = (mt & 3) ? COLOR(145, 205, 255) : COLOR(110, 170, 240);
    FillRect(cx - 45, ey + 14, 2, 40, shC1);
    FillRect(cx + 43, ey + 14, 2, 40, shC1);
    FillRect(cx - 45, ey + 14, 90, 1, shC1);
    /* Shield glow highlights */
    FillRect(cx - 45, ey + 14, 1, 40, shC2);
    FillRect(cx + 44, ey + 14, 1, 40, shC2);
    /* Shield corner nodes */
    FillRect(cx - 45, ey + 14, 3, 3, shC2);
    FillRect(cx + 42, ey + 14, 3, 3, shC2);
    FillRect(cx - 45, ey + 52, 3, 3, shC2);
    FillRect(cx + 42, ey + 52, 3, 3, shC2);
  }

  /* === HP bar - prominent === */
  FillRect(e->x, ey - 11, BOSS_W, 8, COLOR(50, 12, 50));
  FillRect(e->x + 1, ey - 10, (BOSS_W - 2) * e->hp / e->maxHp, 6, COLOR(255, 55, 255));
  FillRect(e->x + 1, ey - 10, (BOSS_W - 2) * e->hp / e->maxHp, 3, COLOR(255, 160, 255));
  /* HP bar border */
  FillRect(e->x, ey - 11, BOSS_W, 1, COLOR(125, 42, 125));
  FillRect(e->x, ey - 4, BOSS_W, 1, COLOR(35, 6, 35));
  /* HP bar tick marks */
  {
    INT32 tick;
    for (tick = 0; tick < 4; tick++) {
      FillRect(e->x + 1 + (BOSS_W - 2) * tick / 4, ey - 10, 1, 6, COLOR(80, 20, 80));
    }
  }
}

static VOID DrawEnemy(Enemy *e)
{
  switch (e->type) {
  case ENEMY_SMALL:  DrawEnemySmall(e); break;
  case ENEMY_MEDIUM: DrawEnemyMedium(e); break;
  case ENEMY_LARGE:  DrawEnemyLarge(e); break;
  case ENEMY_BOSS:   DrawEnemyBoss(e); break;
  default: break;
  }
}

/* --- Power-up items --- */

static VOID DrawPowerUps(GameData *g)
{
  INT32 i;
  for (i = 0; i < MAX_POWERUPS; i++) {
    PowerUp *pu = &g->powerUps[i];
    INT32 px, py, sz;
    UINT32 bgColor, textColor;
    const CHAR8 *label;

    if (!pu->active) continue;

    px = pu->x;
    py = pu->y;
    sz = POWERUP_SIZE;

    /* Pulse animation */
    if ((pu->animTimer & 4) == 0) {
      px -= 1;
      py -= 1;
      sz += 2;
    }

    switch (pu->type) {
    case POWERUP_WEAPON:
      bgColor = COLOR(200, 60, 20);
      textColor = COLOR(255, 255, 200);
      label = "P";
      break;
    case POWERUP_SHIELD:
      bgColor = COLOR(20, 100, 200);
      textColor = COLOR(200, 240, 255);
      label = "S";
      break;
    case POWERUP_BOMB:
      bgColor = COLOR(200, 180, 20);
      textColor = COLOR(80, 40, 0);
      label = "B";
      break;
    case POWERUP_LIFE:
      bgColor = COLOR(20, 180, 40);
      textColor = COLOR(200, 255, 200);
      label = "1UP";
      break;
    case POWERUP_HEAL:
      bgColor = COLOR(200, 60, 140);
      textColor = COLOR(255, 200, 220);
      label = "+";
      break;
    default:
      bgColor = COLOR(128, 128, 128);
      textColor = COLOR(255, 255, 255);
      label = "?";
      break;
    }

    /* Background box */
    FillRect(px, py, sz, sz, bgColor);
    /* Highlight */
    FillRect(px + 1, py + 1, sz - 2, 2, COLOR(255, 255, 255));
    FillRect(px + 1, py + 1, 2, sz - 2, COLOR(255, 255, 255));
    /* Border */
    FillRect(px, py, sz, 1, COLOR(255, 255, 255));
    FillRect(px, py + sz - 1, sz, 1, COLOR(255, 255, 255));
    FillRect(px, py, 1, sz, COLOR(255, 255, 255));
    FillRect(px + sz - 1, py, 1, sz, COLOR(255, 255, 255));
    /* Label */
    DrawCnText(px + 2, py + 2, label, textColor, 1);
  }
}

/* --- Bullets & particles --- */

static VOID DrawBullets(GameData *g)
{
  INT32 i;
  for (i = 0; i < MAX_PLAYER_BULLETS; i++) {
    if (g->pBullets[i].active) {
      INT32 bx = g->pBullets[i].x;
      INT32 by = g->pBullets[i].y;

      if (g->pBullets[i].isWingman) {
        /* Wingman bullet - teal/green energy bolt */
        /* Outer glow */
        FillRect(bx - 1, by + 2, BULLET_W + 2, BULLET_H - 4, COLOR(10, 80, 60));
        /* Bolt body - teal */
        FillRect(bx, by + 1, BULLET_W, BULLET_H - 2, COLOR(40, 200, 160));
        FillRect(bx + 1, by, 2, BULLET_H, COLOR(60, 230, 180));
        /* Core bright */
        FillRect(bx + 1, by + 1, 2, BULLET_H - 2, COLOR(160, 255, 230));
        /* Tip glow */
        FillRect(bx, by - 1, BULLET_W, 2, COLOR(100, 255, 200));
        /* Tail trail - green */
        FillRect(bx + 1, by + BULLET_H, 2, 3, COLOR(30, 180, 120));
        FillRect(bx + 1, by + BULLET_H + 2, 2, 2, COLOR(10, 100, 70));
      } else {
        /* Player bullet - bright blue/white energy bolt */
        /* Outer glow */
        FillRect(bx - 1, by + 3, BULLET_W + 2, BULLET_H - 6, COLOR(60, 140, 200));
        /* Bolt body */
        FillRect(bx + 1, by, 2, BULLET_H, COLOR(200, 230, 255));
        FillRect(bx, by + 2, BULLET_W, BULLET_H - 4, COLOR(150, 220, 255));
        /* Core white-hot */
        FillRect(bx + 1, by + 1, 2, BULLET_H - 2, COLOR(255, 255, 255));
        /* Tip flash */
        FillRect(bx, by - 1, BULLET_W, 2, COLOR(200, 230, 255));
        /* Tail trail */
        FillRect(bx + 1, by + BULLET_H, 2, 4, COLOR(100, 180, 255));
        FillRect(bx + 1, by + BULLET_H + 3, 2, 3, COLOR(40, 100, 180));
      }
    }
  }
  for (i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (g->eBullets[i].active) {
      INT32 ex = g->eBullets[i].x;
      INT32 ey = g->eBullets[i].y;
      INT32 dx = g->eBullets[i].dx;
      INT32 frame = g->frameCount;

      /* === Enemy bullet - large menacing energy orb === */

      /* Far outer glow haze */
      FillRect(ex - 2, ey - 2, 8, 8, COLOR(80, 10, 5));
      /* Outer glow ring */
      FillRect(ex - 1, ey - 1, 6, 6, COLOR(140, 25, 15));
      /* Mid glow layer */
      FillRect(ex, ey, 4, 4, COLOR(220, 50, 30));
      /* Inner hot glow */
      FillRect(ex + 1, ey + 1, 2, 2, COLOR(255, 140, 100));

      /* Pulsing core - alternates between white-hot and orange */
      if (frame & 2) {
        FillRect(ex + 1, ey + 1, 2, 2, COLOR(255, 240, 200));
      } else {
        FillRect(ex + 1, ey + 1, 2, 2, COLOR(255, 200, 100));
      }
      /* Center white dot */
      FillRect(ex + 1, ey + 1, 1, 1, COLOR(255, 255, 240));

      /* Teardrop tail - pointed downward in direction of travel */
      if (dx == 0) {
        /* Straight down - symmetric tail */
        FillRect(ex, ey + 4, 4, 2, COLOR(180, 30, 15));
        FillRect(ex + 1, ey + 5, 2, 2, COLOR(120, 15, 8));
        FillRect(ex + 1, ey + 6, 1, 2, COLOR(80, 8, 4));
      } else if (dx < 0) {
        /* Moving left - angled tail */
        FillRect(ex + 1, ey + 4, 3, 2, COLOR(180, 30, 15));
        FillRect(ex + 2, ey + 5, 2, 2, COLOR(120, 15, 8));
        FillRect(ex, ey + 3, 2, 2, COLOR(100, 12, 6));
      } else {
        /* Moving right - angled tail */
        FillRect(ex, ey + 4, 3, 2, COLOR(180, 30, 15));
        FillRect(ex, ey + 5, 2, 2, COLOR(120, 15, 8));
        FillRect(ex + 2, ey + 3, 2, 2, COLOR(100, 12, 6));
      }

      /* Side flare sparks - animated */
      if (frame & 4) {
        FillRect(ex - 1, ey + 1, 1, 2, COLOR(255, 100, 50));
        FillRect(ex + 4, ey + 1, 1, 2, COLOR(255, 100, 50));
      }
    }
  }
}

static VOID DrawParticles(GameData *g)
{
  INT32 i;
  for (i = 0; i < MAX_PARTICLES; i++) {
    if (g->particles[i].active) {
      INT32 sz = g->particles[i].size;
      if (sz < 1) sz = 1;
      FillRect(g->particles[i].x, g->particles[i].y, sz, sz, g->particles[i].color);
    }
  }
}

static VOID DrawStars(GameData *g)
{
  INT32 i;
  for (i = 0; i < MAX_STARS; i++) {
    UINT32 b = (UINT32)g->stars[i].brightness;
    if (g->stars[i].x >= 0 && g->stars[i].x < (INT32)mScrW &&
        g->stars[i].y >= 0 && g->stars[i].y < (INT32)mScrH) {
      mBackBuffer[g->stars[i].y * mScrW + g->stars[i].x] = COLOR(b, b, b + 30);
    }
  }
}

/* --- HUD --- */

static VOID DrawHUD(GameData *g)
{
  INT32 hudY = 6;
  INT32 i;

  FillRect(0, 0, SCREEN_W, 34, COLOR(8, 8, 20));
  FillRect(0, 34, SCREEN_W, 2, COLOR(60, 60, 120));

  /* Score */
  DrawCnText(10, hudY, "\xe5\xbe\x97\xe5\x88\x86", COLOR(100, 200, 255), 1); /* 得分 */
  DrawCnNum(58, hudY, g->player.score, COLOR(255, 255, 255), 1);

  /* Level */
  DrawCnText(150, hudY, "\xe7\xad\x89\xe7\xba\xa7", COLOR(100, 200, 255), 1); /* 等级 */
  DrawCnNum(198, hudY, g->level, COLOR(255, 255, 100), 1);

  /* Lives */
  DrawCnText(260, hudY, "\xe7\x94\x9f\xe5\x91\xbd", COLOR(100, 200, 255), 1); /* 生命 */
  for (i = 0; i < g->player.lives; i++) {
    FillRect(308 + i * 14, hudY + 2, 10, 10, COLOR(50, 255, 80));
    FillRect(310 + i * 14, hudY + 4, 6, 6, COLOR(100, 255, 140));
  }

  /* HP bar */
  DrawCnText(400, hudY, "\xe6\x8a\xa4\xe7\x94\xb2", COLOR(100, 200, 255), 1); /* 护甲 */
  FillRect(450, hudY + 2, 80, 10, COLOR(60, 20, 20));
  FillRect(450, hudY + 2, 80 * g->player.hp / g->player.maxHp, 10, COLOR(50, 255, 80));
  FillRect(450, hudY + 2, 80 * g->player.hp / g->player.maxHp, 3, COLOR(100, 255, 140));

  /* Weapon level */
  DrawCnText(540, hudY, "\xe7\x81\xab\xe5\x8a\x9b", COLOR(100, 200, 255), 1); /* 火力 */
  for (i = 0; i <= MAX_WEAPON_LEVEL; i++) {
    UINT32 c;
    if (i <= g->player.weaponLevel) {
      c = (i < 3) ? COLOR(255, 200, 60) : COLOR(255, 80, 60);
    } else {
      c = COLOR(40, 40, 50);
    }
    FillRect(590 + i * 12, hudY + 2, 10, 10, c);
    if (i <= g->player.weaponLevel) {
      FillRect(592 + i * 12, hudY + 4, 6, 6, COLOR(255, 255, 200));
    }
  }

  /* Bombs */
  for (i = 0; i < g->player.bombCount; i++) {
    FillRect(660 + i * 14, hudY + 2, 10, 10, COLOR(255, 200, 40));
    FillRect(662 + i * 14, hudY + 4, 6, 6, COLOR(255, 240, 120));
    DrawCnText(663 + i * 14, hudY + 3, "B", COLOR(80, 40, 0), 1);
  }

  /* Shield indicator */
  if (g->player.shieldTimer > 0) {
    DrawCnText(10, hudY + 16, "\xe6\x8a\xa4\xe7\x9b\xbe", COLOR(60, 200, 255), 1); /* 护盾 */
    FillRect(50, hudY + 18, g->player.shieldTimer * 40 / SHIELD_DURATION, 4, COLOR(60, 200, 255));
  }
}

/* --- Screens --- */

static VOID DrawTitleScreen(GameData *g)
{
  INT32 cy = 80;
  INT32 anim = g->titleAnim;

  /* Title with shadow */
  DrawCnText(SCREEN_W / 2 - 62, cy + 4, "\xe5\xa4\xaa\xe7\xa9\xba\xe6\x88\x98\xe6\x9c\xba", COLOR(20, 60, 120), 3);
  DrawCnText(SCREEN_W / 2 - 64, cy, "\xe5\xa4\xaa\xe7\xa9\xba\xe6\x88\x98\xe6\x9c\xba", COLOR(60, 240, 255), 3); /* 太空战机 */

  /* Decorative player ship - detailed */
  {
    INT32 sx = SCREEN_W / 2 - 15;
    INT32 sy = cy + 80;
    /* Wings */
    FillRect(sx, sy + 16, 7, 20, COLOR(25, 90, 180));
    FillRect(sx + 23, sy + 16, 7, 20, COLOR(25, 90, 180));
    FillRect(sx + 2, sy + 18, 4, 14, COLOR(40, 140, 240));
    FillRect(sx + 24, sy + 18, 4, 14, COLOR(40, 140, 240));
    /* Body */
    FillRectGradV(sx + 11, sy + 3, 8, 32, COLOR(100, 210, 255), COLOR(30, 100, 180));
    FillRect(sx + 14, sy + 4, 2, 28, COLOR(180, 240, 255));
    /* Nose */
    FillRect(sx + 13, sy, 4, 4, COLOR(160, 230, 255));
    FillRect(sx + 14, sy, 2, 2, COLOR(255, 255, 255));
    /* Cockpit */
    FillRect(sx + 13, sy + 8, 4, 7, COLOR(160, 230, 255));
    FillRect(sx + 14, sy + 9, 2, 4, COLOR(220, 250, 255));
    /* Tail */
    FillRect(sx + 8, sy + 30, 3, 6, COLOR(30, 100, 200));
    FillRect(sx + 19, sy + 30, 3, 6, COLOR(30, 100, 200));
  }

  /* Animated engine flame on title ship */
  if (anim & 2) {
    INT32 fl = 6 + (anim % 4) * 2;
    FillRectGradV(SCREEN_W / 2 - 4, cy + 116, 3, fl, COLOR(255, 200, 60), COLOR(255, 40, 0));
    FillRectGradV(SCREEN_W / 2 + 1, cy + 116, 3, fl, COLOR(255, 200, 60), COLOR(255, 40, 0));
  }

  /* Controls help */
  DrawCnText(SCREEN_W / 2 - 160, cy + 150,
    "\xe6\x96\xb9\xe5\x90\x91\xe9\x94\xae\xe7\xa7\xbb\xe5\x8a\xa8", COLOR(140, 140, 160), 1); /* 方向键移动 */
  DrawCnText(SCREEN_W / 2 - 80, cy + 175,
    "\xe7\xa9\xba\xe6\xa0\xbc\xe5\xb0\x84\xe5\x87\xbb", COLOR(140, 140, 160), 1); /* 空格射击 */
  DrawCnText(SCREEN_W / 2 - 80, cy + 200,
    "X\xe9\x94\xae\xe6\x8a\x95\xe5\xbc\xb9", COLOR(140, 140, 160), 1); /* X键投弹 */
  DrawCnText(SCREEN_W / 2 - 64, cy + 225,
    "ESC\xe9\x80\x80\xe5\x87\xba", COLOR(140, 140, 160), 1); /* ESC退出 */

  /* Blinking start prompt */
  if ((anim & 16) == 0) {
    DrawCnText(SCREEN_W / 2 - 80, cy + 270,
      "\xe6\x8c\x89\xe5\x9b\x9e\xe8\xbd\xa6\xe5\xbc\x80\xe5\xa7\x8b", COLOR(255, 255, 100), 2); /* 按回车开始 */
  }
}

static VOID DrawGameOverScreen(GameData *g)
{
  FillRect(0, 0, SCREEN_W, SCREEN_H, COLOR(0, 0, 0));

  DrawCnText(SCREEN_W / 2 - 64, 160,
    "\xe6\xb8\xb8\xe6\x88\x8f\xe7\xbb\x93\xe6\x9d\x9f", COLOR(255, 60, 60), 3); /* 游戏结束 */

  DrawCnText(SCREEN_W / 2 - 48, 260,
    "\xe6\x9c\x80\xe7\xbb\x88\xe5\xbe\x97\xe5\x88\x86", COLOR(100, 200, 255), 2); /* 最终得分 */
  DrawCnNum(SCREEN_W / 2 - 16, 300, g->player.score, COLOR(255, 255, 100), 3);

  DrawCnText(SCREEN_W / 2 - 32, 360,
    "\xe7\xad\x89\xe7\xba\xa7", COLOR(100, 200, 255), 1); /* 等级 */
  DrawCnNum(SCREEN_W / 2 + 16, 360, g->level, COLOR(255, 255, 255), 1);

  if (g->gameOverTimer > 60 && (g->frameCount & 16) == 0) {
    DrawCnText(SCREEN_W / 2 - 80, 420,
      "\xe6\x8c\x89\xe5\x9b\x9e\xe8\xbd\xa6\xe9\x87\x8d\xe8\xaf\x95", COLOR(255, 255, 100), 2); /* 按回车重试 */
  }
}

/* ========== Main render ========== */

VOID RenderFrame(GameData *g)
{
  INT32 i;

  ClearBackBuffer(COLOR(6, 4, 18));
  DrawStars(g);

  switch (g->state) {
  case STATE_TITLE:
    DrawTitleScreen(g);
    break;
  case STATE_PLAYING:
    for (i = 0; i < MAX_ENEMIES; i++) {
      if (g->enemies[i].active) DrawEnemy(&g->enemies[i]);
    }
    DrawBullets(g);
    DrawPowerUps(g);
    DrawPlayer(&g->player);
    {
      INT32 wi;
      for (wi = 0; wi < MAX_WINGMEN; wi++) {
        if (g->wingmen[wi].active) {
          DrawWingman(&g->wingmen[wi], g->player.animFrame);
        }
      }
    }
    DrawParticles(g);
    DrawHUD(g);
    break;
  case STATE_GAMEOVER:
    DrawGameOverScreen(g);
    break;
  default: break;
  }

  /* Bomb flash overlay - shockwave effect */
  if (g->bombFlash > 0) {
    INT32 bf = g->bombFlash;
    if (bf > BOMB_DURATION - 3) {
      /* Brief bright flash - only first 3 frames */
      INT32 a = (bf - (BOMB_DURATION - 3)) * 40;
      if (a > 120) a = 120;
      FillRect(0, 0, SCREEN_W, SCREEN_H, COLOR(a, a, a));
    }
    /* Expanding shockwave rings */
    {
      INT32 radius = (BOMB_DURATION - bf) * 18;
      INT32 cx = SCREEN_W / 2;
      INT32 cy = SCREEN_H / 2;
      UINT32 ringC = COLOR(100 + bf * 4, 180 + bf * 2, 255);
      /* Horizontal ring lines */
      if (radius > 0 && radius < SCREEN_W / 2) {
        FillRect(cx - radius, cy - 1, radius * 2, 2, ringC);
        FillRect(cx - radius, cy, radius * 2, 1, COLOR(150 + bf * 3, 220 + bf, 255));
      }
      /* Vertical ring lines */
      if (radius > 0 && radius < SCREEN_H / 2) {
        FillRect(cx - 1, cy - radius, 2, radius * 2, ringC);
      }
      /* Corner bursts */
      if (bf > BOMB_DURATION / 2) {
        FillRect(0, 0, 15, 15, COLOR(bf * 5, bf * 4, 255));
        FillRect(SCREEN_W - 15, 0, 15, 15, COLOR(bf * 5, bf * 4, 255));
        FillRect(0, SCREEN_H - 15, 15, 15, COLOR(bf * 5, bf * 4, 255));
        FillRect(SCREEN_W - 15, SCREEN_H - 15, 15, 15, COLOR(bf * 5, bf * 4, 255));
      }
    }
  }

  Present();
}
