// SPDX-License-Identifier: MIT
#include "http_header_text.h"
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
int main(){
  cef_api_hash(CEF_API_VERSION,0);
  const std::string actual="/empty.html?message=マスクПривет";
  assert(suitecut::HTTPRedirectLocation(actual)==actual);
  assert(suitecut::HTTPHeaderText(actual)!=actual);
  CefString resolved;assert(CefResolveURL("http://example.test/redirect",suitecut::HTTPRedirectLocation(actual),resolved));
  assert(resolved=="http://example.test/empty.html?message=%E3%83%9E%E3%82%B9%E3%82%AF%D0%9F%D1%80%D0%B8%D0%B2%D0%B5%D1%82");
  const std::string latin="/caf\xe9";assert(suitecut::HTTPRedirectLocation(latin)=="/caf\xc3\xa9");
  assert(CefResolveURL("http://example.test/",suitecut::HTTPRedirectLocation(latin),resolved));assert(resolved=="http://example.test/caf%C3%A9");
  for(const std::string& invalid:{std::string("\xc0\xaf",2),std::string("\xe0\x80\xaf",3),std::string("\xed\xa0\x80",3),std::string("\xf4\x90\x80\x80",4),std::string("\xe2\x82",2),std::string("\x80",1)}){
    assert(!suitecut::HTTPValidUTF8(invalid));assert(suitecut::HTTPRedirectLocation(invalid)==suitecut::HTTPHeaderText(invalid));
  }
  for(const std::string& valid:{std::string("ASCII"),std::string("\xc2\xa0",2),std::string("\xef\xbf\xbd",3),std::string("\xf4\x8f\xbf\xbf",4)})assert(suitecut::HTTPValidUTF8(valid));
  std::string encoded;
  assert(suitecut::HTTPEncodeHeaderValue(u"caf\u00e9\t\u00ff",encoded));assert(encoded==std::string("caf\xe9\t\xff",6));
  for(unsigned unit=0;unit<=65535;++unit){const std::u16string value(1,static_cast<char16_t>(unit));const bool valid=unit==9||(unit>=32&&unit<=255&&unit!=127);assert(suitecut::HTTPEncodeHeaderValue(value,encoded)==valid);if(valid)assert(encoded.size()==1&&static_cast<unsigned char>(encoded[0])==unit);else assert(encoded.empty());}
  assert(!suitecut::HTTPEncodeHeaderValue(u"valid-prefix\r\nInjected: header",encoded)&&encoded.empty());
  assert(!suitecut::HTTPEncodeHeaderValue(u"\u043d\u0435\u0434\u043e\u043f\u0443\u0441\u0442\u0438\u043c\u043e\u0435",encoded)&&encoded.empty());
  assert(!suitecut::HTTPEncodeHeaderValue(std::u16string(65537,u'a'),encoded)&&encoded.empty());
  assert(suitecut::HTTPEncodeHeaderValue(std::u16string(65536,u'a'),encoded)&&encoded.size()==65536);
  std::cout<<"Native HTTP Location uses actual UTF-8 before genuine CEF URL resolution; Latin-1 headers, malformed encoding and raw bytes retained\n";
}
