#ifndef __RAYFT_H__
#define __RAYFT_H__

#include <raylib.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#define ATLAS_COUNT 8
#define CACHE_SIZE 128

typedef struct RftAtlas RftAtlas;
typedef struct RftCachedGlyph RftCachedGlyph;
typedef struct RftFont RftFont;

struct RftAtlas {
  Texture2D texture;
  Vector2 next_position;
  int row_height;
};

struct RftCachedGlyph {
  int index;
  FT_Face face;
  int atlas_index;
  Vector2 atlas_position;
  int width;
  int height;
  int x_offset;
  int y_offset;
  float x_advance;
  bool is_color;
  RftCachedGlyph* next;
};

struct RftFont {
  FT_Face face;
  FT_Face fallback_face;
  RftAtlas atlases[ATLAS_COUNT];
  int used_atlases;
  RftCachedGlyph* cache[CACHE_SIZE];
};

void RftInit(void);
Color* RftBitmapToRGBA(FT_Bitmap* bitmap);
RftAtlas RftCreateAtlas(void);
void RftDestroyAtlas(RftAtlas atlas);
bool RftAtlasHasSpace(RftAtlas atlas, Vector2 dim);
Vector2 RftAddToAtlas(RftAtlas* atlas, Color* pixels, Vector2 dim);
RftFont RftLoadFont(char* path, int size);
bool RftSetFallbackFont(RftFont* font, char* path);
void RftUnloadFont(RftFont font);
int RftHash(int x);
RftCachedGlyph* RftGetGlyph(RftFont* font, int index);
RftCachedGlyph* RftFindGlyph(RftFont font, int index);
RftCachedGlyph* RftLoadGlyph(RftFont* font, int index);
Vector2 RftDrawGlyph(RftFont* font, int index, Vector2 position, Color color);
void RftDrawText(RftFont* font, const char* text, Vector2 position, Color color);

#endif
