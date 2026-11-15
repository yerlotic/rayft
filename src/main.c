#include <raylib.h>
#include "lib/rayft.h"

#define FONT_SIZE 24

int main(void) {
  InitWindow(600, 400, "RayFT");
  SetTargetFPS(60);

  RftInit();
  RftFont font = RftLoadFont("assets/DMMono-Regular.ttf", FONT_SIZE);
  if (!RftSetFallbackFont(&font, "/usr/share/fonts/noto/NotoColorEmoji.ttf")) {
    TraceLog(LOG_WARNING, "Could not load Noto Color Emoji. Set the fallback font path in src/main.c.");
  }
  const char *text = "Hello, world!🧑‍💻 🏳️‍🌈 👨‍👩‍👧‍👦  😀";

  while (!WindowShouldClose()) {
    BeginDrawing();
    ClearBackground(RAYWHITE);
    RftDrawText(&font, text, (Vector2){ 100, 100 }, BLACK);
    EndDrawing();
  }

  RftUnloadFont(font);
  CloseWindow();
  return 0;
}
