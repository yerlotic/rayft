#include "rayft.h"

#define ATLAS_SIZE 1024

static FT_Library ft;

// Initializes the RayFT library. Call this function before loading/drawing any font.
void RftInit(void) {
  FT_Init_FreeType(&ft);
}

// Converts a FreeType bitmap into RGBA pixels for the atlas texture. FreeType's
// BGRA color bitmaps use premultiplied alpha, while Raylib expects straight RGBA.
Color* RftBitmapToRGBA(FT_Bitmap* bitmap) {
  int w = bitmap->width;
  int h = bitmap->rows;
  Color* pixels = (Color*) malloc((size_t)w * h * sizeof(Color));
  int pitch = bitmap->pitch;
  int row_stride = pitch >= 0 ? pitch : -pitch;

  for (int y = 0; y < h; y++) {
    int source_y = pitch >= 0 ? y : h - 1 - y;
    const unsigned char* row = bitmap->buffer + source_y * row_stride;
    for (int x = 0; x < w; x++) {
      Color color;
      if (bitmap->pixel_mode == FT_PIXEL_MODE_BGRA) {
        const unsigned char* bgra = row + x * 4;
        unsigned int alpha = bgra[3];
        // Un-premultiply the channels for standard alpha blending.
        color = (Color){
          .r = alpha ? (unsigned char)((bgra[2] * 255U + alpha / 2U) / alpha) : 0,
          .g = alpha ? (unsigned char)((bgra[1] * 255U + alpha / 2U) / alpha) : 0,
          .b = alpha ? (unsigned char)((bgra[0] * 255U + alpha / 2U) / alpha) : 0,
          .a = (unsigned char)alpha
        };
      } else {
        unsigned char alpha = 0;
        if (bitmap->pixel_mode == FT_PIXEL_MODE_MONO) {
          alpha = (row[x >> 3] & (0x80 >> (x & 7))) ? 255 : 0;
        } else {
          alpha = row[x];
          if (bitmap->pixel_mode == FT_PIXEL_MODE_GRAY && bitmap->num_grays > 1 && bitmap->num_grays != 256) {
            alpha = (unsigned char)((unsigned int)alpha * 255U / (bitmap->num_grays - 1));
          }
        }
        color = (Color){ .r = 255, .g = 255, .b = 255, .a = alpha };
      }
      pixels[y * w + x] = color;
    }
  }
  return pixels;
}

// Create an empty atlas
RftAtlas RftCreateAtlas(void) {
  Image image = GenImageColor(ATLAS_SIZE, ATLAS_SIZE, BLANK);
  RftAtlas atlas = (RftAtlas) {
    .texture = LoadTextureFromImage(image),
    .next_position = (Vector2){ 0, 0 },
    .row_height = 0
  };
  UnloadImage(image);
  SetTextureFilter(atlas.texture, TEXTURE_FILTER_BILINEAR);
  return atlas;
}

// Destroy the atlas (unload its texture)
void RftDestroyAtlas(RftAtlas atlas) {
  UnloadTexture(atlas.texture);
}

// Checks whether there is an available position in the atlas for the given dimensions
bool RftAtlasHasSpace(RftAtlas atlas, Vector2 dim) {
  Vector2 next = atlas.next_position;
  if (next.x + dim.x < ATLAS_SIZE && next.y + dim.y < ATLAS_SIZE) {
    return true;
  }
  if (dim.x < ATLAS_SIZE && next.y + atlas.row_height + dim.y < ATLAS_SIZE) {
    return true;
  }
  return false;
}

// Draws the given pixels at the next available position in the atlas (if present),
// following the provided dimensions. Returns the position at which the glyph was placed,
// which is placed at (-1, -1) if the placement was unsuccessful
Vector2 RftAddToAtlas(RftAtlas* atlas, Color* pixels, Vector2 dim) {
  Vector2 next = atlas->next_position;
  if (next.x + dim.x < ATLAS_SIZE && next.y + dim.y < ATLAS_SIZE) {
    // Place it at the next position on the same row
    Rectangle target = (Rectangle) {
      .x = next.x,
      .y = next.y,
      .width = dim.x,
      .height = dim.y
    };
    UpdateTextureRec(atlas->texture, target, pixels);
    // Update the current row's height if necessary
    if (dim.y > atlas->row_height) {
      atlas->row_height = dim.y;
    }
    // Update next position
    atlas->next_position = (Vector2){ next.x + dim.x, next.y };
    return (Vector2){ next.x, next.y };
  } else if (dim.x < ATLAS_SIZE && next.y + atlas->row_height + dim.y < ATLAS_SIZE) {
    // Place it at the beginning of a new row
    Rectangle target = (Rectangle){
      .x = 0,
      .y = next.y + atlas->row_height,
      .width = dim.x,
      .height = dim.y
    };
    UpdateTextureRec(atlas->texture, target, pixels);
    // Update next position
    atlas->next_position = (Vector2){ dim.x, next.y + atlas->row_height };
    // Set the initial row height
    atlas->row_height = dim.y;
    return (Vector2){ 0, next.y + atlas->row_height };
  }
  return (Vector2){ -1, -1 };
}

// Loads the font at the provided path and sets its initial size
RftFont RftLoadFont(char* path, int size) {
  RftFont font;
  FT_Face face;
  FT_New_Face(ft, path, 0, &face);
  FT_Select_Charmap(face, FT_ENCODING_UNICODE);
  FT_Set_Pixel_Sizes(face, 0, size);
  font.face = face;
  for (int i = 0; i < CACHE_SIZE; i++) {
    font.cache[i] = NULL;
  }
  font.atlases[0] = RftCreateAtlas();
  font.used_atlases = 1;
  return font;
}

// Removes the font's resources from memory
void RftUnloadFont(RftFont font) {
  for (int i = 0; i < CACHE_SIZE; i++) {
    RftCachedGlyph* next_glyph = font.cache[i];
    RftCachedGlyph* glyph = NULL;
    while (next_glyph != NULL) {
      glyph = next_glyph;
      next_glyph = glyph->next;
      free(glyph);
    }
  }
  for (int i = 0; i < ATLAS_COUNT; i++) {
    RftDestroyAtlas(font.atlases[i]);
  }
  FT_Done_Face(font.face);
}

// Returns the hash value for x
int RftHash(int x) {
    x ^= x >> 16;
    x *= 0x85ebca6bU;
    x ^= x >> 13;
    x *= 0xc2b2ae35U;
    x ^= x >> 16;
    return x;
}

// Returns the font glyph at the provided index, loading it and adding it to the cache
// if it wasn't already there.
RftCachedGlyph* RftGetGlyph(RftFont* font, int index) {
  RftCachedGlyph* g = RftFindGlyph(*font, index);
  if (g == NULL) {
    g = RftLoadGlyph(font, index);
  }
  return g;
}

// Returns the cached info for the glyph at the given index if present, otherwise NULL.
RftCachedGlyph* RftFindGlyph(RftFont font, int index) {
  for (RftCachedGlyph* g = font.cache[RftHash(index) % CACHE_SIZE]; g != NULL; g = g->next) {
    if (g->index == index) {
      return g;
    }
  }
  return NULL;
}

// Loads and caches the glyph at the given index, returning a pointer to it.
RftCachedGlyph* RftLoadGlyph(RftFont* font, int index) {
  // Color emoji fonts such as Noto Color Emoji use bitmap strikes; force-autohinting
  // can make FreeType fail to load those glyphs.
  FT_Load_Glyph(font->face, index, FT_LOAD_RENDER | FT_LOAD_COLOR);
  FT_GlyphSlot glyph = font->face->glyph;
  FT_Bitmap* bitmap = &glyph->bitmap;
  int width = bitmap->width;
  int height = bitmap->rows;
  int x_offset = glyph->bitmap_left;
  int y_offset = glyph->bitmap_top;
  float x_advance = glyph->advance.x / 64.0f;
  Color* pixels = RftBitmapToRGBA(bitmap);
  // Find an available position in the current atlas, if there isn't one,
  // create a new one.
  Vector2 dim = (Vector2){ width, height };
  Vector2 atlas_position;
  int atlas_index;
  if (!RftAtlasHasSpace(font->atlases[font->used_atlases - 1], dim)) {
    atlas_index = font->used_atlases;
    font->atlases[font->used_atlases] = RftCreateAtlas();
    font->used_atlases += 1;
  } else {
    atlas_index = font->used_atlases - 1;
  }
  atlas_position = RftAddToAtlas(&font->atlases[atlas_index], pixels, dim);
  free(pixels);
  // Create glyph
  RftCachedGlyph* cached = (RftCachedGlyph*) malloc(sizeof(RftCachedGlyph));
  cached->atlas_index = atlas_index;
  cached->atlas_position = atlas_position;
  cached->index = index;
  cached->x_advance = x_advance;
  cached->x_offset = x_offset;
  cached->y_offset = y_offset;
  cached->width = width;
  cached->height = height;
  cached->is_color = bitmap->pixel_mode == FT_PIXEL_MODE_BGRA;
  // Insert into cache
  int hash = RftHash(index) % CACHE_SIZE;
  cached->next = font->cache[hash];
  font->cache[hash] = cached;
  return cached;
}

// Draws the glyph at the given index at the given position. Returns the position at which the
// next glyph should be drawn on the same line.
Vector2 RftDrawGlyph(RftFont* font, int index, Vector2 position, Color color) {
  RftCachedGlyph* glyph = RftGetGlyph(font, index);
  float baseline_y = position.y + font->face->size->metrics.ascender / 64.0f;
  float draw_x = position.x + glyph->x_offset;
  float draw_y = baseline_y - glyph->y_offset;
  RftAtlas atlas = font->atlases[glyph->atlas_index];
  Rectangle source = (Rectangle){ glyph->atlas_position.x, glyph->atlas_position.y, glyph->width, glyph->height };
  Rectangle target = (Rectangle){ draw_x, draw_y, glyph->width, glyph->height };
  DrawTexturePro(atlas.texture, source, target, (Vector2){ 0, 0 }, 0.0f, glyph->is_color ? WHITE : color);
  return (Vector2){ position.x + glyph->x_advance, position.y };
}

// Draws the text at the given position using the specified font and color.
void RftDrawText(RftFont* font, const char* text, Vector2 position, Color color) {
  char* ptr = (char*) text;
  Vector2 current_position = position;
  while (*ptr != '\0') {
    int byte_count;
    int codepoint = GetCodepointNext(ptr, &byte_count);
    FT_UInt glyph_index = FT_Get_Char_Index(font->face, codepoint);
    ptr += byte_count;
    // TODO: check for line break
    current_position = RftDrawGlyph(font, glyph_index, current_position, color);
  }
}
