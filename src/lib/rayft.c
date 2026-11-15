#include "rayft.h"

#define ATLAS_SIZE 1024

static FT_Library ft;

void RftInit(void) {
  FT_Init_FreeType(&ft);
}

Color* RftBitmapToRGBA(FT_Bitmap* bitmap) {
  int w = bitmap->width, h = bitmap->rows;
  Color* pixels = malloc((size_t)w * h * sizeof(Color));
  int stride = abs(bitmap->pitch);
  for (int y = 0; y < h; y++) {
    const unsigned char* row = bitmap->buffer + (bitmap->pitch >= 0 ? y : h - 1 - y) * stride;
    for (int x = 0; x < w; x++) {
      if (bitmap->pixel_mode == FT_PIXEL_MODE_BGRA) {
        const unsigned char* p = row + x * 4;
        pixels[y*w+x] = (Color){ p[2], p[1], p[0], p[3] };
      } else {
        unsigned char a = bitmap->pixel_mode == FT_PIXEL_MODE_MONO
          ? ((row[x >> 3] & (0x80 >> (x & 7))) ? 255 : 0) : row[x];
        pixels[y*w+x] = (Color){ 255, 255, 255, a };
      }
    }
  }
  return pixels;
}

RftAtlas RftCreateAtlas(void) {
  Image image = GenImageColor(ATLAS_SIZE, ATLAS_SIZE, BLANK);
  RftAtlas atlas = { .texture = LoadTextureFromImage(image), .next_position = { 0, 0 }, .row_height = 0 };
  UnloadImage(image);
  SetTextureFilter(atlas.texture, TEXTURE_FILTER_BILINEAR);
  return atlas;
}

void RftDestroyAtlas(RftAtlas atlas) { UnloadTexture(atlas.texture); }

bool RftAtlasHasSpace(RftAtlas atlas, Vector2 dim) {
  Vector2 next = atlas.next_position;
  return (next.x + dim.x < ATLAS_SIZE && next.y + dim.y < ATLAS_SIZE) ||
         (dim.x < ATLAS_SIZE && next.y + atlas.row_height + dim.y < ATLAS_SIZE);
}

Vector2 RftAddToAtlas(RftAtlas* atlas, Color* pixels, Vector2 dim) {
  Vector2 next = atlas->next_position;
  float y = next.y;
  if (next.x + dim.x >= ATLAS_SIZE || next.y + dim.y >= ATLAS_SIZE) {
    if (dim.x >= ATLAS_SIZE || next.y + atlas->row_height + dim.y >= ATLAS_SIZE) return (Vector2){ -1, -1 };
    next.x = 0;
    y += atlas->row_height;
    atlas->row_height = 0;
  }
  UpdateTextureRec(atlas->texture, (Rectangle){ next.x, y, dim.x, dim.y }, pixels);
  if (dim.y > atlas->row_height) atlas->row_height = dim.y;
  atlas->next_position = (Vector2){ next.x + dim.x, y };
  return (Vector2){ next.x, y };
}

RftFont RftLoadFont(char* path, int size) {
  RftFont font = { 0 };
  FT_New_Face(ft, path, 0, &font.face);
  FT_Select_Charmap(font.face, FT_ENCODING_UNICODE);
  FT_Set_Pixel_Sizes(font.face, 0, size);
  font.atlases[0] = RftCreateAtlas();
  font.used_atlases = 1;
  return font;
}

bool RftSetFallbackFont(RftFont* font, char* path) {
  if (FT_New_Face(ft, path, 0, &font->fallback_face)) return false;
  FT_Select_Charmap(font->fallback_face, FT_ENCODING_UNICODE);
  return FT_Set_Pixel_Sizes(font->fallback_face, 0, font->face->size->metrics.y_ppem) == 0;
}

void RftUnloadFont(RftFont font) {
  for (int i = 0; i < CACHE_SIZE; i++) {
    RftCachedGlyph* glyph = font.cache[i];
    while (glyph) { RftCachedGlyph* next = glyph->next; free(glyph); glyph = next; }
  }
  for (int i = 0; i < font.used_atlases; i++) RftDestroyAtlas(font.atlases[i]);
  if (font.fallback_face) FT_Done_Face(font.fallback_face);
  FT_Done_Face(font.face);
}

int RftHash(int x) {
  x ^= x >> 16; x *= 0x85ebca6bU; x ^= x >> 13; x *= 0xc2b2ae35U; x ^= x >> 16;
  return x;
}

static RftCachedGlyph* find_glyph(RftFont* font, int index, FT_Face face) {
  for (RftCachedGlyph* g = font->cache[RftHash(index) % CACHE_SIZE]; g; g = g->next)
    if (g->index == index && g->face == face) return g;
  return NULL;
}

static RftCachedGlyph* load_glyph(RftFont* font, int index, FT_Face face) {
  if (FT_Load_Glyph(face, index, FT_LOAD_RENDER | FT_LOAD_COLOR)) return NULL;
  FT_GlyphSlot slot = face->glyph;
  FT_Bitmap* bitmap = &slot->bitmap;
  int width = bitmap->width, height = bitmap->rows;
  Color* pixels = RftBitmapToRGBA(bitmap);
  int atlas_index = font->used_atlases - 1;
  if (!RftAtlasHasSpace(font->atlases[atlas_index], (Vector2){ width, height })) {
    if (atlas_index + 1 >= ATLAS_COUNT) { free(pixels); return NULL; }
    atlas_index = font->used_atlases++;
    font->atlases[atlas_index] = RftCreateAtlas();
  }
  Vector2 atlas_position = RftAddToAtlas(&font->atlases[atlas_index], pixels, (Vector2){ width, height });
  free(pixels);
  RftCachedGlyph* cached = malloc(sizeof(*cached));
  *cached = (RftCachedGlyph){ .index = index, .face = face, .atlas_index = atlas_index,
    .atlas_position = atlas_position, .width = width, .height = height,
    .x_offset = slot->bitmap_left, .y_offset = slot->bitmap_top,
    .x_advance = slot->advance.x / 64.0f,
    .is_color = bitmap->pixel_mode == FT_PIXEL_MODE_BGRA };
  int hash = RftHash(index) % CACHE_SIZE;
  cached->next = font->cache[hash];
  font->cache[hash] = cached;
  return cached;
}

RftCachedGlyph* RftFindGlyph(RftFont font, int index) { return find_glyph(&font, index, font.face); }
RftCachedGlyph* RftLoadGlyph(RftFont* font, int index) { return load_glyph(font, index, font->face); }
RftCachedGlyph* RftGetGlyph(RftFont* font, int index) {
  RftCachedGlyph* glyph = find_glyph(font, index, font->face);
  return glyph ? glyph : load_glyph(font, index, font->face);
}

static Vector2 draw_glyph(RftFont* font, int index, FT_Face face, Vector2 position, Color color) {
  RftCachedGlyph* glyph = find_glyph(font, index, face);
  if (!glyph) glyph = load_glyph(font, index, face);
  if (!glyph) return position;
  float baseline = position.y + face->size->metrics.ascender / 64.0f;
  Rectangle source = { glyph->atlas_position.x, glyph->atlas_position.y, glyph->width, glyph->height };
  Rectangle target = { position.x + glyph->x_offset, baseline - glyph->y_offset, glyph->width, glyph->height };
  DrawTexturePro(font->atlases[glyph->atlas_index].texture, source, target, (Vector2){ 0, 0 }, 0, glyph->is_color ? WHITE : color);
  return (Vector2){ position.x + glyph->x_advance, position.y };
}

Vector2 RftDrawGlyph(RftFont* font, int index, Vector2 position, Color color) {
  return draw_glyph(font, index, font->face, position, color);
}

void RftDrawText(RftFont* font, const char* text, Vector2 position, Color color) {
  const char* ptr = text;
  while (*ptr) {
    int bytes, codepoint = GetCodepointNext(ptr, &bytes);
    FT_Face face = font->face;
    FT_UInt index = FT_Get_Char_Index(face, codepoint);
    if (!index && font->fallback_face) {
      face = font->fallback_face;
      index = FT_Get_Char_Index(face, codepoint);
    }
    ptr += bytes;
    if (index) position = draw_glyph(font, index, face, position, color);
  }
}
