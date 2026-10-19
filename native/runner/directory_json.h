// SPDX-License-Identifier: MIT
#pragma once
#include <charconv>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
namespace suitecut::runner {
class DirectoryJSONBuffer {
 public:
  static constexpr size_t kBytes=64*1024*1024;
  void Put(std::string_view value){if(value.size()>kBytes-bytes.size())throw std::length_error("Native directory JSON capacity64MiB exceeded");bytes.append(value);}
  template<class T>void NumberValue(T value){char buffer[128];const auto result=std::to_chars(buffer,buffer+sizeof(buffer),value);if(result.ec!=std::errc{})throw std::invalid_argument("Native directory number encoding rejected");Put({buffer,static_cast<size_t>(result.ptr-buffer)});}
  void DecimalValue(double value){char buffer[128];const auto result=std::to_chars(buffer,buffer+sizeof(buffer),value,std::chars_format::general,std::numeric_limits<double>::max_digits10);if(result.ec!=std::errc{})throw std::invalid_argument("Native directory clock encoding rejected");Put({buffer,static_cast<size_t>(result.ptr-buffer)});}
  void Hex(std::string_view value){constexpr char hex[]="0123456789abcdef";if(value.size()>(kBytes-bytes.size())/2)throw std::length_error("Native directory hex capacity exceeded");for(unsigned char c:value){bytes+=hex[c>>4];bytes+=hex[c&15];}}
  std::string bytes;
};
}
