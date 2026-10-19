// SPDX-License-Identifier: MIT
#include <suitecut/image.hpp>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Bytes=std::vector<std::uint8_t>;
void check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
template<class F> void rejects(F operation){bool failed=false;try{operation();}catch(const std::exception&){failed=true;}check(failed,"Expected image rejection");}
void put(Bytes& out,std::uint32_t value){for(int shift:{24,16,8,0})out.push_back(static_cast<std::uint8_t>(value>>shift));}
void chunk(Bytes& out,const char* name,const Bytes& data){put(out,data.size());const auto start=out.size();out.insert(out.end(),name,name+4);out.insert(out.end(),data.begin(),data.end());put(out,crc32(0,out.data()+start,data.size()+4));}
Bytes compressed(const Bytes& raw){uLongf size=compressBound(raw.size());Bytes out(size);check(compress2(out.data(),&size,raw.data(),raw.size(),Z_BEST_SPEED)==Z_OK,"Fixture zlib failure");out.resize(size);return out;}
Bytes header(unsigned w,unsigned h,unsigned depth,unsigned color,unsigned interlace=0){Bytes out;put(out,w);put(out,h);out.insert(out.end(),{static_cast<std::uint8_t>(depth),static_cast<std::uint8_t>(color),0,0,static_cast<std::uint8_t>(interlace)});return out;}
Bytes png(unsigned w,unsigned h,unsigned depth,unsigned color,const Bytes& raw,unsigned interlace=0,
          const Bytes& palette={},const Bytes& alpha={}){
  Bytes out{137,80,78,71,13,10,26,10};chunk(out,"IHDR",header(w,h,depth,color,interlace));
  if(!palette.empty())chunk(out,"PLTE",palette);
  if(!alpha.empty())chunk(out,"tRNS",alpha);
  chunk(out,"IDAT",compressed(raw));chunk(out,"IEND",{});return out;
}
void pixels(const Bytes& encoded,const Bytes& expected){check(suitecut::decode_png(encoded).rgba==expected,"Decoded pixels differ from independent fixture");}
unsigned paeth(unsigned a,unsigned b,unsigned c){const int p=int(a)+int(b)-int(c),x=std::abs(p-int(a)),y=std::abs(p-int(b)),z=std::abs(p-int(c));return x<=y&&x<=z?a:y<=z?b:c;}
Bytes filtered_rgba(const Bytes& rgba,unsigned w,unsigned h){Bytes out;const auto stride=w*4;for(unsigned y=0;y<h;++y){const auto f=y%5;out.push_back(f);for(unsigned i=0;i<stride;++i){const unsigned a=i>=4?rgba[y*stride+i-4]:0,b=y?rgba[(y-1)*stride+i]:0,c=y&&i>=4?rgba[(y-1)*stride+i-4]:0;const unsigned p=f==0?0:f==1?a:f==2?b:f==3?(a+b)/2:paeth(a,b,c);out.push_back(static_cast<std::uint8_t>(rgba[y*stride+i]-p));}}return out;}
Bytes adam7(const Bytes& rgba,unsigned w,unsigned h){constexpr unsigned passes[7][4]={{0,0,8,8},{4,0,8,8},{0,4,4,8},{2,0,4,4},{0,2,2,4},{1,0,2,2},{0,1,1,2}};Bytes raw;for(const auto& p:passes)for(unsigned y=p[1];y<h;y+=p[3]){if(p[0]>=w)continue;raw.push_back(0);for(unsigned x=p[0];x<w;x+=p[2])raw.insert(raw.end(),rgba.begin()+(y*w+x)*4,rgba.begin()+(y*w+x+1)*4);}return raw;}
} // namespace

int main(){try{
  suitecut::PngImage original{9,7,{}};
  for(unsigned y=0;y<original.height;++y)for(unsigned x=0;x<original.width;++x)
    original.rgba.insert(original.rgba.end(),{static_cast<std::uint8_t>(x*37+y*11),static_cast<std::uint8_t>(x*3+y*77),static_cast<std::uint8_t>(x*17+y*23),static_cast<std::uint8_t>(x*13+y*31)});
  const auto encoded=suitecut::encode_png(original);
  const auto decoded=suitecut::decode_png(encoded);
  check(decoded.width==9&&decoded.height==7&&decoded.rgba==original.rgba,"RGBA PNG roundtrip");
  pixels(png(9,7,8,6,filtered_rgba(original.rgba,9,7)),original.rgba);
  pixels(png(9,7,8,6,adam7(original.rgba,9,7),1),original.rgba);
  pixels(png(1,1,8,6,{0,20,40,60,80},1),{20,40,60,80});
  {constexpr unsigned passes[7][4]={{0,0,8,8},{4,0,8,8},{0,4,4,8},{2,0,4,4},{0,2,2,4},{1,0,2,2},{0,1,1,2}};
    Bytes raw,expected;
    for(unsigned y=0;y<9;++y)for(unsigned x=0;x<9;++x){const auto gray=static_cast<std::uint8_t>(((x+y)%4)*85);expected.insert(expected.end(),{gray,gray,gray,255});}
    for(const auto& p:passes)for(unsigned y=p[1];y<9;y+=p[3]){raw.push_back(0);unsigned byte=0,bits=0;
      for(unsigned x=p[0];x<9;x+=p[2]){byte=(byte<<2)|((x+y)%4);bits+=2;if(bits==8){raw.push_back(byte);byte=bits=0;}}
      if(bits)raw.push_back(byte<<(8-bits));}
    pixels(png(9,9,2,0,raw,1),expected);
  }
  pixels(png(9,1,1,0,{0,0x55,0x80}),{0,0,0,255,255,255,255,255,0,0,0,255,255,255,255,255,0,0,0,255,255,255,255,255,0,0,0,255,255,255,255,255,255,255,255,255});
  pixels(png(4,1,2,0,{0,0x1b}),{0,0,0,255,85,85,85,255,170,170,170,255,255,255,255,255});
  pixels(png(2,1,4,0,{0,0x0f}),{0,0,0,255,255,255,255,255});
  pixels(png(2,1,8,0,{0,37,38},0,{}, {0,37}),{37,37,37,0,38,38,38,255});
  pixels(png(2,1,16,0,{0,0xab,0xcd,0x12,0x34},0,{}, {0xab,0xcd}),{0xab,0xab,0xab,0,0x12,0x12,0x12,255});
  pixels(png(2,1,8,2,{0,1,2,3,4,5,6},0,{}, {0,1,0,2,0,3}),{1,2,3,0,4,5,6,255});
  pixels(png(1,1,16,2,{0,0x12,0x34,0xab,0xcd,0,255}),{0x12,0xab,0,255});
  pixels(png(1,1,8,4,{0,30,44}),{30,30,30,44});
  pixels(png(1,1,16,4,{0,0xab,0x12,0xcd,0x34}),{0xab,0xab,0xab,0xcd});
  pixels(png(1,1,16,6,{0,0x12,0x34,0xab,0xcd,0,255,0x80,0}),{0x12,0xab,0,0x80});
  const Bytes palette{255,0,0,0,255,0,0,0,255,255,255,255};
  const Bytes palette_pixels{255,0,0,0,0,255,0,64,0,0,255,128,255,255,255,255};
  pixels(png(4,1,2,3,{0,0x1b},0,palette,{0,64,128}),palette_pixels);
  pixels(png(2,1,1,3,{0,0x40},0,{255,0,0,0,255,0}),{255,0,0,255,0,255,0,255});
  pixels(png(2,1,4,3,{0,0x01},0,{255,0,0,0,255,0}),{255,0,0,255,0,255,0,255});
  pixels(png(2,1,8,3,{0,0,1},0,{255,0,0,0,255,0}),{255,0,0,255,0,255,0,255});

  auto comparison=suitecut::compare_png(encoded,encoded);
  check(comparison.equal&&comparison.dimensions_match&&comparison.compared_pixels==63&&comparison.changed_pixels==0&&comparison.changed_ratio==0&&comparison.diff_png.empty(),"Equal image comparison");
  auto changed=original;changed.rgba[0]+=5;changed.rgba[4]+=6;changed.rgba[11]+=7;
  const auto altered=suitecut::encode_png(changed);
  suitecut::ImageCompareOptions options;options.channel_tolerance=5;options.make_diff=true;
  comparison=suitecut::compare_png(encoded,altered,options);
  check(!comparison.equal&&comparison.changed_pixels==2&&comparison.compared_pixels==63&&std::abs(comparison.changed_ratio-2.0/63)<1e-12,"Per-channel tolerance and exact changed ratio");
  const auto diff=suitecut::decode_png(comparison.diff_png);
  check(diff.width==9&&diff.height==7&&diff.rgba[3]==0&&diff.rgba[4]==255&&diff.rgba[5]==0&&diff.rgba[6]==255&&diff.rgba[7]==255&&diff.rgba[11]==255,"Diff PNG contains transparent unchanged and magenta changed pixels");
  options.include_alpha=false;comparison=suitecut::compare_png(encoded,altered,options);
  check(comparison.changed_pixels==1,"Alpha comparison can be excluded");
  options.channel_tolerance=255;check(suitecut::compare_png(encoded,altered,options).equal,"Maximum channel tolerance");
  const auto one=suitecut::encode_png(suitecut::PngImage{1,1,{20,40,60,80}});
  const auto two=suitecut::encode_png(suitecut::PngImage{2,1,{20,40,60,80,20,40,60,80}});
  comparison=suitecut::compare_png(one,two,options);
  check(!comparison.equal&&!comparison.dimensions_match&&comparison.expected_width==1&&comparison.actual_width==2&&comparison.changed_pixels==1&&comparison.compared_pixels==2&&comparison.changed_ratio==0.5,"Missing pixels count changed even with tolerance 255");
  const auto tall=suitecut::encode_png(suitecut::PngImage{1,2,{20,40,60,80,20,40,60,80}});
  comparison=suitecut::compare_png(two,tall,options);
  check(comparison.compared_pixels==4&&comparison.changed_pixels==3&&comparison.changed_ratio==0.75,"Crossed dimensions use documented bounding canvas");

  for(std::size_t n=0;n<one.size();++n)rejects([&]{suitecut::decode_png(Bytes(one.begin(),one.begin()+n));});
  auto bad=one;bad[bad.size()-1]^=1;rejects([&]{suitecut::decode_png(bad);});
  bad=one;bad.push_back(0);rejects([&]{suitecut::decode_png(bad);});
  rejects([&]{suitecut::decode_png(png(1,1,8,6,{5,1,2,3,4}));});
  rejects([&]{suitecut::decode_png(png(1,1,8,6,{0,1,2,3}));});
  rejects([&]{suitecut::decode_png(png(1,1,8,6,{0,1,2,3,4,5}));});
  rejects([&]{suitecut::decode_png(png(0,1,8,6,{0}));});
  rejects([&]{suitecut::decode_png(png(32769,1,8,6,{0}));});
  rejects([&]{suitecut::decode_png(png(1,1,4,6,{0}));});
  rejects([&]{suitecut::decode_png(png(1,1,8,5,{0}));});
  rejects([&]{suitecut::decode_png(png(1,1,8,6,{0},2));});
  rejects([&]{suitecut::decode_png(png(1,1,2,3,{0,0xc0}));});
  rejects([&]{suitecut::decode_png(png(1,1,2,3,{0,0xc0},0,{255,0,0}));});
  rejects([&]{suitecut::decode_png(png(1,1,8,6,{0,1,2,3,4},0,{}, {0}));});
  rejects([&]{suitecut::decode_png(png(1,1,1,0,{0,0},0,{}, {0,2}));});
  const auto construct=[&](const char* extra,bool after_data=false){Bytes p{137,80,78,71,13,10,26,10};chunk(p,"IHDR",header(1,1,8,6));if(!after_data)chunk(p,extra,{});chunk(p,"IDAT",compressed({0,1,2,3,4}));if(after_data)chunk(p,extra,{});chunk(p,"IEND",{});return p;};
  rejects([&]{suitecut::decode_png(construct("ABCD"));});
  rejects([&]{suitecut::decode_png(construct("abcd"));});
  rejects([&]{suitecut::decode_png(construct("acTL"));});
  pixels(construct("tEXt"),{1,2,3,4});
  pixels(construct("tEXt",true),{1,2,3,4});
  {Bytes p{137,80,78,71,13,10,26,10};chunk(p,"IHDR",header(1,1,8,6));chunk(p,"IHDR",header(1,1,8,6));chunk(p,"IDAT",compressed({0,1,2,3,4}));chunk(p,"IEND",{});rejects([&]{suitecut::decode_png(p);});}
  {Bytes p{137,80,78,71,13,10,26,10};chunk(p,"IHDR",header(1,1,8,6));auto z=compressed({0,1,2,3,4});const auto half=z.size()/2;chunk(p,"IDAT",Bytes(z.begin(),z.begin()+half));chunk(p,"tEXt",{});chunk(p,"IDAT",Bytes(z.begin()+half,z.end()));chunk(p,"IEND",{});rejects([&]{suitecut::decode_png(p);});}
  {Bytes p{137,80,78,71,13,10,26,10};chunk(p,"IHDR",header(1,1,8,6));auto z=compressed({0,1,2,3,4});const auto half=z.size()/2;chunk(p,"IDAT",Bytes(z.begin(),z.begin()+half));chunk(p,"IDAT",Bytes(z.begin()+half,z.end()));chunk(p,"IEND",{});pixels(p,{1,2,3,4});}
  {Bytes p{137,80,78,71,13,10,26,10};chunk(p,"IHDR",header(1,1,8,6));auto z=compressed({0,1,2,3,4});z.push_back(0);chunk(p,"IDAT",z);chunk(p,"IEND",{});rejects([&]{suitecut::decode_png(p);});}
  {Bytes p{137,80,78,71,13,10,26,10};chunk(p,"IHDR",header(1,1,8,6));auto z=compressed({0,1,2,3,4});z.back()^=1;chunk(p,"IDAT",z);chunk(p,"IEND",{});rejects([&]{suitecut::decode_png(p);});}
  suitecut::ImageLimits limits;limits.max_dimension=1;rejects([&]{suitecut::decode_png(two,limits);});
  limits={};limits.max_pixels=1;rejects([&]{suitecut::decode_png(two,limits);});
  limits={};limits.max_decoded_bytes=4;rejects([&]{suitecut::decode_png(one,limits);});
  limits={};limits.max_encoded_bytes=one.size()-1;rejects([&]{suitecut::decode_png(one,limits);});
  limits={};limits.max_encoded_bytes=20;rejects([&]{suitecut::encode_png(original,limits);});
  limits={};limits.max_dimension=32769;rejects([&]{suitecut::decode_png(one,limits);});
  rejects([&]{suitecut::encode_png(suitecut::PngImage{2,1,{1,2,3,4}});});
  options={};options.limits.max_pixels=3;rejects([&]{suitecut::compare_png(two,tall,options);});

  // Deterministic mutation exercises framing, CRC rejection and limits. A valid
  // mutation must still produce an exactly sized bounded image, never a partial.
  std::uint32_t random=0x19a7b33d;const auto next=[&]{random^=random<<13;random^=random>>17;random^=random<<5;return random;};
  std::size_t accepted=0;
  for(unsigned iteration=0;iteration<10000;++iteration){Bytes mutation=iteration%2?encoded:one;
    switch(iteration%4){case 0:mutation.resize(next()%(mutation.size()+1));break;case 1:mutation[next()%mutation.size()]^=static_cast<std::uint8_t>(1+(next()%255));break;case 2:mutation.insert(mutation.begin()+next()%(mutation.size()+1),static_cast<std::uint8_t>(next()));break;default:mutation.erase(mutation.begin()+next()%mutation.size());}
    std::optional<suitecut::PngImage> image;
    try{image=suitecut::decode_png(mutation);}catch(const std::exception&){}
    if(image){check(image->rgba.size()==std::size_t(image->width)*image->height*4,"Mutation returned partial image");++accepted;}
  }
  // CRC-correct mutations reach the inflater/filter/palette paths too.
  for(unsigned i=0;i<2000;++i){Bytes raw(1+next()%96);for(auto& b:raw)b=static_cast<std::uint8_t>(next());std::optional<suitecut::PngImage> image;try{image=suitecut::decode_png(png(1+next()%8,1+next()%4,8,6,raw));}catch(const std::exception&){}if(image)check(image->rgba.size()==std::size_t(image->width)*image->height*4,"Inflate mutation returned partial image");}
  std::cout<<"native image checks passed; 12000 bounded mutations; accepted="<<accepted<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
