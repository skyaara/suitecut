// SPDX-License-Identifier: MIT
#include "suitecut/image.hpp"
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>

namespace suitecut {
namespace {
using Bytes = std::vector<std::uint8_t>;
constexpr std::array<std::uint8_t, 8> signature{137,80,78,71,13,10,26,10};
[[noreturn]] void invalid(const char* reason) { throw std::runtime_error(std::string("Invalid PNG: ") + reason); }
void validate_limits(const ImageLimits& l) {
  if (!l.max_dimension || l.max_dimension > 32768 || !l.max_pixels || l.max_pixels > 64 * 1024 * 1024 ||
      !l.max_encoded_bytes || l.max_encoded_bytes > 128 * 1024 * 1024 ||
      !l.max_decoded_bytes || l.max_decoded_bytes > 256 * 1024 * 1024)
    throw std::invalid_argument("Invalid native image limits");
}
std::size_t pixels(std::uint32_t w, std::uint32_t h, const ImageLimits& l) {
  const std::uint64_t count = std::uint64_t(w) * h;
  if (!w || !h || w > l.max_dimension || h > l.max_dimension || count > l.max_pixels ||
      count * 4 > l.max_decoded_bytes) throw std::runtime_error("PNG dimensions exceed native image limits");
  return static_cast<std::size_t>(count);
}
std::uint32_t u32(const std::uint8_t* p) {
  return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
unsigned u16(const std::uint8_t* p) { return (unsigned(p[0]) << 8) | p[1]; }
void put32(Bytes& out, std::uint32_t n) {
  for (int shift : {24,16,8,0}) out.push_back(static_cast<std::uint8_t>(n >> shift));
}
void chunk(Bytes& out, const char* name, const Bytes& data, const ImageLimits& limits) {
  if (out.size() > limits.max_encoded_bytes || data.size() > limits.max_encoded_bytes - out.size() ||
      limits.max_encoded_bytes - out.size() - data.size() < 12)
    throw std::runtime_error("Encoded PNG exceeds native image limit");
  put32(out, static_cast<std::uint32_t>(data.size()));
  const auto start = out.size();
  out.insert(out.end(), name, name + 4); out.insert(out.end(), data.begin(), data.end());
  put32(out, static_cast<std::uint32_t>(crc32(0, out.data() + start, static_cast<uInt>(data.size() + 4))));
}
struct Pass { unsigned x, y, dx, dy; };
constexpr std::array<Pass,7> adam7{{{0,0,8,8},{4,0,8,8},{0,4,4,8},{2,0,4,4},{0,2,2,4},{1,0,2,2},{0,1,1,2}}};
unsigned extent(unsigned n, unsigned start, unsigned step) { return n <= start ? 0 : 1 + (n - start - 1) / step; }
int channels(unsigned color) {
  switch (color) { case 0:return 1; case 2:return 3; case 3:return 1; case 4:return 2; case 6:return 4; }
  invalid("unsupported color type");
}
bool depth_valid(unsigned color, unsigned depth) {
  if (color == 0) return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
  if (color == 3) return depth == 1 || depth == 2 || depth == 4 || depth == 8;
  return (color == 2 || color == 4 || color == 6) && (depth == 8 || depth == 16);
}
unsigned sample(const Bytes& row, std::size_t index, unsigned depth) {
  if (depth == 16) return u16(row.data() + index * 2);
  if (depth == 8) return row[index];
  const auto bit = index * depth;
  return (row[bit / 8] >> (8 - depth - bit % 8)) & ((1U << depth) - 1);
}
std::uint8_t normalized(unsigned value, unsigned depth) {
  return static_cast<std::uint8_t>(depth == 16 ? value >> 8 : value * 255 / ((1U << depth) - 1));
}
unsigned paeth(unsigned a, unsigned b, unsigned c) {
  const int p = static_cast<int>(a + b) - static_cast<int>(c);
  const int da = std::abs(p - static_cast<int>(a)), db = std::abs(p - static_cast<int>(b)), dc = std::abs(p - static_cast<int>(c));
  return da <= db && da <= dc ? a : db <= dc ? b : c;
}
} // namespace

PngImage decode_png(const Bytes& bytes, ImageLimits limits) {
  validate_limits(limits);
  if (bytes.size() > limits.max_encoded_bytes) invalid("encoded byte limit");
  if (bytes.size() < signature.size() || !std::equal(signature.begin(), signature.end(), bytes.begin())) invalid("signature");
  unsigned depth = 0, color = 0, interlace = 0;
  PngImage image;
  Bytes compressed, palette, transparency;
  bool ihdr = false, plte = false, trns = false, idat = false, idat_ended = false, end = false;
  std::size_t offset = 8, chunks = 0;
  while (offset < bytes.size()) {
    if (++chunks > 65536 || bytes.size() - offset < 12) invalid("truncated or excessive chunks");
    const auto size = u32(bytes.data() + offset);
    if (size > 0x7fffffff || size > bytes.size() - offset - 12) invalid("chunk length");
    const auto* name = bytes.data() + offset + 4;
    for (int i = 0; i < 4; ++i) if (!((name[i] >= 'A' && name[i] <= 'Z') || (name[i] >= 'a' && name[i] <= 'z'))) invalid("chunk name");
    if (name[2] & 32) invalid("reserved chunk bit");
    if (crc32(0, name, static_cast<uInt>(size + 4)) != u32(name + 4 + size)) invalid("chunk CRC");
    const std::string type(reinterpret_cast<const char*>(name), 4);
    const auto* data = name + 4;
    if (!ihdr && type != "IHDR") invalid("IHDR must be first");
    if (idat && type != "IDAT") idat_ended = true;
    if (type == "IHDR") {
      if (ihdr || size != 13) invalid("IHDR length or duplicate");
      image.width = u32(data); image.height = u32(data + 4);
      pixels(image.width, image.height, limits);
      depth = data[8]; color = data[9]; interlace = data[12];
      if (!depth_valid(color, depth) || data[10] || data[11] || interlace > 1) invalid("IHDR format");
      ihdr = true;
    } else if (type == "PLTE") {
      if (plte || trns || idat || color == 0 || color == 4 || size == 0 || size > 768 || size % 3 ||
          (color == 3 && size / 3 > (1U << depth))) invalid("PLTE format or order");
      palette.assign(data, data + size); plte = true;
    } else if (type == "tRNS") {
      if (trns || idat || color == 4 || color == 6 || (color == 0 && size != 2) ||
          (color == 2 && size != 6) || (color == 3 && (!plte || size > palette.size() / 3))) invalid("tRNS format or order");
      if (color == 0 && u16(data) >= (1U << depth)) invalid("tRNS sample");
      if (color == 2) for (int i = 0; i < 3; ++i) if (u16(data + i * 2) >= (1U << depth)) invalid("tRNS sample");
      transparency.assign(data, data + size); trns = true;
    } else if (type == "IDAT") {
      if (idat_ended || (color == 3 && !plte)) invalid("IDAT order or missing palette");
      compressed.insert(compressed.end(), data, data + size); idat = true;
    } else if (type == "IEND") {
      if (!idat || size) invalid("IEND format or order");
      end = true; offset += 12; break;
    } else if (type == "acTL" || type == "fcTL" || type == "fdAT") invalid("animated PNG is unsupported");
    else if (!(name[0] & 32)) invalid("unknown critical chunk");
    offset += static_cast<std::size_t>(size) + 12;
  }
  if (!end || offset != bytes.size() || compressed.empty()) invalid("missing end/data or trailing bytes");
  const auto count = pixels(image.width, image.height, limits);
  const auto n_channels = static_cast<unsigned>(channels(color));
  std::array<Pass,7> passes = adam7;
  const unsigned pass_count = interlace ? 7 : 1;
  if (!interlace) passes[0] = {0,0,1,1};
  std::size_t inflated_size = 0;
  for (unsigned i = 0; i < pass_count; ++i) {
    const auto p = passes[i];
    const auto w = extent(image.width,p.x,p.dx), h = extent(image.height,p.y,p.dy);
    if (!w || !h) continue;
    const std::uint64_t row = (std::uint64_t(w) * n_channels * depth + 7) / 8;
    const auto contribution = (row + 1) * h;
    if (contribution > limits.max_decoded_bytes - inflated_size) invalid("inflated byte limit");
    inflated_size += static_cast<std::size_t>(contribution);
  }
  Bytes inflated(inflated_size + 1);
  z_stream stream{};
  if (inflateInit(&stream) != Z_OK) throw std::runtime_error("Cannot initialize PNG inflater");
  stream.next_in = const_cast<Bytef*>(compressed.data()); stream.avail_in = static_cast<uInt>(compressed.size());
  stream.next_out = inflated.data(); stream.avail_out = static_cast<uInt>(inflated.size());
  const int status = inflate(&stream, Z_FINISH);
  const bool valid = status == Z_STREAM_END && stream.total_out == inflated_size && stream.total_in == compressed.size();
  inflateEnd(&stream);
  if (!valid) invalid("zlib stream length/checksum or trailing data");
  image.rgba.resize(count * 4);
  std::size_t cursor = 0;
  for (unsigned pass_index = 0; pass_index < pass_count; ++pass_index) {
    const auto p = passes[pass_index];
    const auto w = extent(image.width,p.x,p.dx), h = extent(image.height,p.y,p.dy);
    if (!w || !h) continue;
    const std::size_t row_size = (std::uint64_t(w) * n_channels * depth + 7) / 8;
    const std::size_t bpp = std::max(1U, (n_channels * depth + 7) / 8);
    Bytes previous(row_size), row(row_size);
    for (unsigned y = 0; y < h; ++y) {
      const unsigned filter = inflated[cursor++];
      if (filter > 4) invalid("scanline filter");
      for (std::size_t j = 0; j < row_size; ++j) {
        const unsigned left = j >= bpp ? row[j-bpp] : 0, up = previous[j], diagonal = j >= bpp ? previous[j-bpp] : 0;
        const unsigned predictor = filter == 0 ? 0 : filter == 1 ? left : filter == 2 ? up :
                                   filter == 3 ? (left + up) / 2 : paeth(left,up,diagonal);
        row[j] = static_cast<std::uint8_t>(inflated[cursor++] + predictor);
      }
      for (unsigned x = 0; x < w; ++x) {
        const auto base = static_cast<std::size_t>(x) * n_channels;
        unsigned r=0,g=0,b=0,a=255;
        if (color == 3) {
          const auto index = sample(row,base,depth);
          if (index >= palette.size()/3) invalid("palette index");
          r=palette[index*3];g=palette[index*3+1];b=palette[index*3+2];
          if (index < transparency.size()) a=transparency[index];
        } else if (color == 0 || color == 4) {
          const auto gray = sample(row,base,depth);
          r=g=b=normalized(gray,depth);
          if (color == 4) a=normalized(sample(row,base+1,depth),depth);
          else if (trns && gray == u16(transparency.data())) a=0;
        } else {
          const auto red=sample(row,base,depth), green=sample(row,base+1,depth), blue=sample(row,base+2,depth);
          r=normalized(red,depth);g=normalized(green,depth);b=normalized(blue,depth);
          if (color == 6) a=normalized(sample(row,base+3,depth),depth);
          else if (trns && red == u16(transparency.data()) && green == u16(transparency.data()+2) && blue == u16(transparency.data()+4)) a=0;
        }
        const auto out = (static_cast<std::size_t>(p.y+y*p.dy)*image.width + p.x+x*p.dx)*4;
        image.rgba[out]=static_cast<std::uint8_t>(r);image.rgba[out+1]=static_cast<std::uint8_t>(g);
        image.rgba[out+2]=static_cast<std::uint8_t>(b);image.rgba[out+3]=static_cast<std::uint8_t>(a);
      }
      previous.swap(row);
    }
  }
  if (cursor != inflated_size) invalid("scanline accounting");
  return image;
}

Bytes encode_png(const PngImage& image, ImageLimits limits) {
  validate_limits(limits);
  if (limits.max_encoded_bytes < 58) throw std::runtime_error("Encoded PNG exceeds native image limit");
  const auto count = pixels(image.width,image.height,limits);
  if (image.rgba.size() != count*4) throw std::invalid_argument("PNG RGBA byte count differs from dimensions");
  const std::size_t stride = static_cast<std::size_t>(image.width)*4;
  const std::uint64_t raw_size = std::uint64_t(stride+1)*image.height;
  if (raw_size > limits.max_decoded_bytes) throw std::runtime_error("PNG scanlines exceed native image limit");
  Bytes raw(static_cast<std::size_t>(raw_size));
  for (unsigned y=0;y<image.height;++y)
    std::copy_n(image.rgba.data()+std::size_t(y)*stride,stride,raw.data()+std::size_t(y)*(stride+1)+1);
  uLongf compressed_size=std::min<uLongf>(compressBound(static_cast<uLong>(raw.size())),limits.max_encoded_bytes-57);
  Bytes compressed(compressed_size);
  if (compress2(compressed.data(),&compressed_size,raw.data(),static_cast<uLong>(raw.size()),Z_BEST_SPEED)!=Z_OK)
    throw std::runtime_error("PNG compression failed or encoded byte limit exceeded");
  compressed.resize(compressed_size);
  Bytes out(signature.begin(),signature.end()), header;
  put32(header,image.width);put32(header,image.height);header.insert(header.end(),{8,6,0,0,0});
  chunk(out,"IHDR",header,limits);chunk(out,"IDAT",compressed,limits);chunk(out,"IEND",{},limits);
  return out;
}

ImageComparison compare_png(const Bytes& expected, const Bytes& actual, ImageCompareOptions options) {
  const auto a=decode_png(expected,options.limits), b=decode_png(actual,options.limits);
  ImageComparison result;
  result.expected_width=a.width;result.expected_height=a.height;result.actual_width=b.width;result.actual_height=b.height;
  result.dimensions_match=a.width==b.width&&a.height==b.height;
  const auto width=std::max(a.width,b.width),height=std::max(a.height,b.height);
  result.compared_pixels=pixels(width,height,options.limits);
  PngImage diff{width,height,{}};
  if(options.make_diff)diff.rgba.resize(static_cast<std::size_t>(result.compared_pixels)*4);
  for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
    bool changed=x>=a.width||y>=a.height||x>=b.width||y>=b.height;
    if(!changed)for(unsigned channel=0;channel<(options.include_alpha?4U:3U);++channel) {
      const auto av=a.rgba[(std::size_t(y)*a.width+x)*4+channel],bv=b.rgba[(std::size_t(y)*b.width+x)*4+channel];
      if(std::abs(int(av)-int(bv))>options.channel_tolerance){changed=true;break;}
    }
    if(changed) {
      ++result.changed_pixels;
      if(options.make_diff){const auto i=(std::size_t(y)*width+x)*4;diff.rgba[i]=255;diff.rgba[i+2]=255;diff.rgba[i+3]=255;}
    }
  }
  result.changed_ratio=static_cast<double>(result.changed_pixels)/static_cast<double>(result.compared_pixels);
  result.equal=result.dimensions_match&&result.changed_pixels==0;
  if(options.make_diff)result.diff_png=encode_png(diff,options.limits);
  return result;
}
} // namespace suitecut
