#include "types.h"

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS Status;
  GameData Game;
  EFI_EVENT TimerEvent;
  UINTN Index;

  Print(L"\r\n=== SHMUP - Space Fighter ===\r\n");

  /* Initialize graphics */
  Status = GraphicsInit();
  if (EFI_ERROR(Status)) {
    Print(L"Error: GraphicsInit failed: %r\r\n", Status);
    return Status;
  }

  /* Initialize input */
  InputInit();

  /* Set up title screen */
  SetMem(&Game, sizeof(Game), 0);
  Game.state = STATE_TITLE;
  Game.titleAnim = 0;

  ShmupSeed(42);
  InitStars(&Game);

  /* Create periodic timer (~16ms = ~60fps) */
  Status = gBS->CreateEvent(EVT_TIMER, TPL_CALLBACK, NULL, NULL, &TimerEvent);
  if (EFI_ERROR(Status)) {
    Print(L"Error: CreateEvent failed: %r\r\n", Status);
    GraphicsCleanup();
    return Status;
  }

  Status = gBS->SetTimer(TimerEvent, TimerPeriodic, 8 * 10000); /* 8ms ~125fps */
  if (EFI_ERROR(Status)) {
    Print(L"Error: SetTimer failed: %r\r\n", Status);
    GraphicsCleanup();
    return Status;
  }

  Print(L"Game loop started. Press ESC to exit.\r\n");

  /* Main game loop */
  while (!Game.quitRequested) {
    Status = gBS->WaitForEvent(1, &TimerEvent, &Index);
    if (EFI_ERROR(Status)) continue;

    InputUpdate(&Game);
    GameUpdate(&Game);
    RenderFrame(&Game);
  }

  /* Cleanup */
  gBS->CloseEvent(TimerEvent);
  GraphicsCleanup();

  Print(L"\r\n=== SHMUP exited ===\r\n");
  return EFI_SUCCESS;
}
