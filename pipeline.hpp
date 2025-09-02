#pragma once
#include "PPU466.hpp"
#include <glm/glm.hpp>

#include <iostream>
#include <array>
#include <vector>
#include <stdexcept>
#include <string>
#include "data_path.hpp"
#include "read_write_chunk.hpp"
#include "load_save_png.hpp"
#include <stdint.h>

struct PackedTiles {
  PPU466::Palette palette;           
  std::vector<PPU466::Tile> tiles;  
};

PackedTiles build_palette_and_tiles( 
    glm::uvec2 size, 
    const std::vector<glm::u8vec4> &pixels); 

struct SpriteAsset {
  std::string name;
  std::vector<PPU466::Palette> palettes; // usually 1
  std::vector<PPU466::Tile>    tiles;    // 1+ tiles (8×8 each)
};


PackedTiles build_palette_and_tiles(
    glm::uvec2 size,
    const std::vector<glm::u8vec4>& pixels
) {

  PPU466::Palette palette{};
  palette[0] = glm::u8vec4(0,0,0,0); // transparent

  std::vector<glm::u8vec4> unique_clr; 
  unique_clr.reserve(3);

  for (const auto& px : pixels) {

    if (px.a < 128) continue; 

    //find unique opaque colors:
    glm::u8vec4 c = {px.r, px.g, px.b, 0xff}; 
    bool seen = false;

    for (auto& u : unique_clr) { 
        if (u == c) {
             seen = true; break; 
        } 
    }

    if (!seen) {
      unique_clr.push_back(c);
      if (unique_clr.size() > 3) {
        throw std::runtime_error("Image has more than 3 unique colors.");
      }
    }
  }

  for (size_t i = 0; i < unique_clr.size(); ++i){
    palette[1 + i] = unique_clr[i];
  }

  for (size_t i = unique_clr.size(); i < palette.size(); ++i){
    palette[1 + i] = glm::u8vec4(0,0,0,0);
  }
   
  auto palette_index_of = [&](glm::u8vec4 px) -> uint8_t {
    if (px.a < 128) return 0; 
    glm::u8vec4 c = {px.r, px.g, px.b, 0xff};
    for (uint8_t i = 0; i < 3; ++i) {
      if (palette[1 + i] == c) return uint8_t(1 + i);
    }

    throw std::runtime_error("Found opaque pixel not in 3-color palette.");
  };

    // Break image into 8x8 tiles:
  const uint32_t tiles_x = size.x / 8u;
  const uint32_t tiles_y = size.y / 8u;

  std::vector<PPU466::Tile> tiles;
  tiles.resize(tiles_x * tiles_y);

  auto src_index = [&](uint32_t x, uint32_t y) -> size_t {
    return size_t(y) * size.x + x;
  };

  for (uint32_t ty = 0; ty < tiles_y; ++ty) {
    for (uint32_t tx = 0; tx < tiles_x; ++tx) {
      PPU466::Tile tile{};

      for (uint32_t y = 0; y < 8u; ++y) {
        uint8_t b0 = 0;
        uint8_t b1 = 0;
        uint32_t img_y = ty * 8u + y; 
        for (uint32_t x = 0; x < 8u; ++x) {
          uint32_t img_x = tx * 8u + x;
          glm::u8vec4 px = pixels[src_index(img_x, img_y)];
          uint8_t idx = palette_index_of(px); 

          b0 |= ((idx & 1u) ? 1u : 0u) << x;
          b1 |= ((idx & 2u) ? 1u : 0u) << x;
        }
        tile.bit0[y] = b0;
        tile.bit1[y] = b1;
      }
      tiles[ty * tiles_x + tx] = tile;
    }
  }
    return PackedTiles{palette, tiles};
}


// Ranges into the pooled arrays (half-open [begin,end) in element units):
struct StoredSprite {
  uint32_t name_begin, name_end;   // chars in 'names'
  uint32_t pals_begin, pals_end;   // palettes in 'pals'
  uint32_t tiles_begin, tiles_end; // tiles in 'tiles'
};
static_assert(sizeof(StoredSprite) == 24, "packed");

void write_sprite_assets(std::vector<SpriteAsset> const& assets, std::ostream *to_) {
  assert(to_);
  auto &to = *to_;

  std::vector<char> names;
  std::vector<PPU466::Palette>pals;
  std::vector<PPU466::Tile> tiles;
  std::vector<StoredSprite> metas;

  for (auto const& a : assets) {
    StoredSprite m{};

    m.name_begin = uint32_t(names.size());
    names.insert(names.end(), a.name.begin(), a.name.end());
    m.name_end = uint32_t(names.size());

    m.pals_begin = uint32_t(pals.size());
    pals.insert(pals.end(), a.palettes.begin(), a.palettes.end());
    m.pals_end = uint32_t(pals.size());

    m.tiles_begin = uint32_t(tiles.size());
    tiles.insert(tiles.end(), a.tiles.begin(), a.tiles.end());
    m.tiles_end = uint32_t(tiles.size());

    metas.emplace_back(m);
  }

  write_chunk("name", names, &to);
  write_chunk("pal0", pals,  &to); 
  write_chunk("tile", tiles, &to);
  write_chunk("sprt", metas, &to); 
}

std::vector<SpriteAsset> read_sprite_assets(std::istream &from) {

  std::vector<char> names;
  std::vector<PPU466::Palette> pals;
  std::vector<PPU466::Tile> tiles;
  std::vector<StoredSprite> metas;

  read_chunk(from, "name", &names);
  read_chunk(from, "pal0", &pals);
  read_chunk(from, "tile", &tiles);
  read_chunk(from, "sprt", &metas);

  char extra;
  if (from.read(&extra, 1)) throw std::runtime_error("Trailing junk at end of file.");

  std::vector<SpriteAsset> out;
  out.reserve(metas.size());
  for (auto const& m : metas) {
    if (!(m.name_begin <= m.name_end && m.name_end <= names.size()))
      throw std::runtime_error("Bad name range.");
    if (!(m.pals_begin <= m.pals_end && m.pals_end <= pals.size()))
      throw std::runtime_error("Bad palette range.");
    if (!(m.tiles_begin <= m.tiles_end && m.tiles_end <= tiles.size()))
      throw std::runtime_error("Bad tile range.");

    SpriteAsset a;

    // name:
    a.name.assign(names.begin() + m.name_begin, names.begin() + m.name_end);

    // palettes:
    a.palettes.assign(pals.begin() + m.pals_begin, pals.begin() + m.pals_end);

    // tiles:
    a.tiles.assign(tiles.begin() + m.tiles_begin, tiles.begin() + m.tiles_end);

    out.emplace_back(std::move(a));
  }
  return out;
}
