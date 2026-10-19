// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "har_multipart.h"
#include "http_multipart.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
using suitecut::NativeHARMultipartEquivalent;
struct Field {std::string name,bytes;bool file=false;};
std::string Body(const std::string&boundary,const std::vector<Field>&fields){
  std::vector<unsigned char>input,output;std::vector<suitecut::NativeHTTPMultipartPart>parts;std::string error;
  for(const auto&field:fields){parts.push_back({field.name,field.file?"actual.bin":"",field.file?"application/octet-stream":"",input.size(),field.bytes.size(),field.file});input.insert(input.end(),field.bytes.begin(),field.bytes.end());}
  assert(suitecut::EncodeNativeHTTPMultipart(input,parts,boundary,2097152,output,error));return {output.begin(),output.end()};
}
int main(){
  const std::string a="recordedBoundary0123456789",b="actualBoundary9876543210";
  const auto type_a="multipart/form-data; boundary="+a,type_b="multipart/form-data; boundary="+b;
  std::string binary;for(unsigned i=0;i<256;++i)binary+=static_cast<char>(i);
  binary+="\r\n--"+a+"-literal-file-bytes\r\n";
  const std::vector<Field>fields={{"repeated","first"},{"repeated","second"},{"file",binary,true}};
  const auto left=Body(a,fields),right=Body(b,fields);
  assert(left!=right&&NativeHARMultipartEquivalent(left,type_a,right,type_b));
  assert(NativeHARMultipartEquivalent(left," Multipart/Form-Data ; boundary=\""+a+"\" ",right,type_b));
  assert(NativeHARMultipartEquivalent(left,"multipart/form-data;charset=utf-8;boundary="+a,right,"multipart/form-data;boundary=\""+b+"\";charset=utf-8"));
  assert(!NativeHARMultipartEquivalent(left,"multipart/form-data;charset=utf-8;boundary="+a,right,"multipart/form-data;boundary="+b+";charset=latin1"));
  auto changed=fields;changed[2].bytes.replace(changed[2].bytes.find(a),a.size(),b);assert(!NativeHARMultipartEquivalent(left,type_a,Body(b,changed),type_b));
  changed=fields;std::swap(changed[0],changed[1]);assert(!NativeHARMultipartEquivalent(left,type_a,Body(b,changed),type_b));
  changed=fields;changed.erase(changed.begin());assert(!NativeHARMultipartEquivalent(left,type_a,Body(b,changed),type_b));
  changed=fields;changed[2].bytes[0]=1;assert(!NativeHARMultipartEquivalent(left,type_a,Body(b,changed),type_b));
  // Different quoting applies only to the actual outer boundary parameter.
  const auto tiny=[](std::string_view boundary){return "--"+std::string(boundary)+"\r\nContent-Disposition: form-data; name=\"x\"\r\n\r\nactual\r\n--"+std::string(boundary)+"--\r\n";};
  assert(NativeHARMultipartEquivalent(tiny("a:b"),"multipart/form-data; boundary=\"a\\:b\"",tiny("c:d"),"multipart/form-data; boundary=\"c:d\""));
  assert(NativeHARMultipartEquivalent(tiny("a"),"multipart/form-data; boundary=a",tiny("b"),"multipart/form-data; boundary=b"));
  assert(NativeHARMultipartEquivalent("--"+a+"--\r\n",type_a,"--"+b+"--\r\n",type_b));
  for(const auto&bad:std::vector<std::string>{"", "text/plain;boundary="+a,"multipart/mixed;boundary="+a,"multipart/form-data", "multipart/form-data;boundary=", "multipart/form-data;boundary=\"\"",type_a+";BOUNDARY="+a,R"(multipart/form-data;boundary="a ")", "multipart/form-data;boundary=\"a\r\nb\"", "multipart/form-data;boundary=\"unterminated", "multipart/form-data;boundary=bad:token", "multipart/form-data;boundary="+std::string(71,'a'),type_a+";broken"})
    assert(!NativeHARMultipartEquivalent(left,bad,right,type_b));
  assert(!NativeHARMultipartEquivalent(left,type_a,right.substr(0,right.size()-3),type_b));
  assert(!NativeHARMultipartEquivalent(left,type_a,right.substr(0,right.size()-2),type_b));
  assert(!NativeHARMultipartEquivalent(left,type_a,"preamble\r\n"+right,type_b));
  auto malformed=right;malformed.replace(malformed.find("content-disposition"),19,"unrelated-header-xxx");assert(!NativeHARMultipartEquivalent(left,type_a,malformed,type_b));
  const auto duplicate="--"+b+"\r\nContent-Disposition: form-data; name=\"x\"\r\nContent-Disposition: form-data; name=\"y\"\r\n\r\nactual\r\n--"+b+"--\r\n";
  assert(!NativeHARMultipartEquivalent(tiny(a),type_a,duplicate,type_b));
  // A delimiter-shaped byte sequence inside a file cannot be rewritten into
  // another boundary. It either remains literal data or changes MIME framing.
  const std::vector<Field>collision={{"file","prefix\r\n--"+a+"\r\nnot-a-part-header\r\nbody",true}};
  assert(!NativeHARMultipartEquivalent(Body(a,collision),type_a,Body(b,collision),type_b));
  const auto original_left=left,original_right=right;assert(NativeHARMultipartEquivalent(left,type_a,right,type_b));assert(left==original_left&&right==original_right);
  std::cout<<"PASS native multipart boundary equivalence with encoder-derived binary data, repeated ordered fields, strict types/framing and unchanged raw bytes\n";
}
