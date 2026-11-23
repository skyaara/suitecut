// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#include <iterator>
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include "trace/trace_dom_tree.h"
#include "debugger/devtools_utf16.h"
int main(int argc,char** argv){
  assert(cef_api_hash(CEF_API_VERSION,0));auto raw=CefParseJSON(R"({"strings":["frame-a","https://example.test/","#document","HTML","HEAD","IFRAME","BODY","BUTTON","hello","id","target","red",""],"documents":[{"frameId":0,"documentURL":1,"nodes":{"nodeType":[9,1,1,1,1,1,3],"nodeName":[2,3,4,5,6,7,12],"nodeValue":[12,12,12,12,12,12,8],"parentIndex":[-1,0,1,2,1,4,5],"attributes":[[],[],[],[],[],[9,10],[]]},"layout":{"nodeIndex":[5],"styles":[[11]]}}]})",JSON_PARSER_RFC);
  if(argc==3){std::ifstream input(argv[1],std::ios::binary);const std::string bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());auto actual=CefParseJSON(bytes,JSON_PARSER_RFC);assert(actual&&actual->GetDictionary());suitecut::TraceDOMTree actual_tree;std::vector<suitecut::TraceDOMTree::Document> actual_docs;std::string actual_error;assert(actual_tree.Convert(actual->GetDictionary(),{"color"},actual_docs,actual_error));auto rendered=CefListValue::Create();for(size_t i=0;i<actual_docs.size();++i){auto item=CefDictionaryValue::Create();item->SetString("frame",actual_docs[i].frame);item->SetString("url",actual_docs[i].url);item->SetString("htmlJSON",actual_docs[i].html_json);rendered->SetDictionary(i,item);}auto encoded=CefValue::Create();encoded->SetList(rendered);std::ofstream output(argv[2],std::ios::binary);output<<CefWriteJSON(encoded,JSON_WRITER_DEFAULT).ToString();assert(output.good());}
  suitecut::TraceDOMTree tree;std::vector<suitecut::TraceDOMTree::Document> docs;std::string error;assert(tree.Convert(raw->GetDictionary(),{"color"},docs,error));assert(docs.size()==1&&docs[0].frame=="frame-a"&&docs[0].url=="https://example.test/");const auto json=docs[0].html_json;assert(json.find("BUTTON")!=std::string::npos&&json.find("hello")!=std::string::npos&&json.find("color:red;")!=std::string::npos&&json.find("IFRAME")==std::string::npos);
  auto nodes=raw->GetDictionary()->GetList("documents")->GetDictionary(0)->GetDictionary("nodes");nodes->GetList("parentIndex")->SetInt(6,6);assert(!tree.Convert(raw->GetDictionary(),{"color"},docs,error));assert(!error.empty());nodes->GetList("parentIndex")->SetInt(6,5);nodes->GetList("nodeName")->SetInt(6,999);assert(!tree.Convert(raw->GetDictionary(),{"color"},docs,error));
  // These are actual Inspector string-table scalars. The native typed writer
  // must preserve isolated keys/values and a literal sidecar-looking attribute.
  std::string projected,projection_error;const std::string input=R"({"strings":["frame-a","https://example.test/","#document","DIV","","__suitecutNativeUTF16Scalar","d800","\ud800","\udc00","style","color:red;"],"documents":[{"frameId":0,"documentURL":1,"nodes":{"nodeType":[9,1,3],"nodeName":[2,3,4],"nodeValue":[4,4,7],"parentIndex":[-1,0,1],"attributes":[[],[5,6,7,8,9,10],[]]},"layout":{"nodeIndex":[1],"styles":[[10]]}}]})";
  assert(suitecut::DevToolsLosslessProjection(input,projected,projection_error));auto units=CefParseJSON(projected,JSON_PARSER_RFC);assert(units);assert(tree.Convert(units->GetDictionary(),{"color"},docs,error));const auto exact=docs[0].html_json;
  assert(exact.find("\"__suitecutNativeUTF16Scalar\":\"d800\"")!=std::string::npos);
  assert(exact.find("\"\\ud800\":\"\\udc00\"")!=std::string::npos);
  assert(exact.find(",\"\\ud800\"]")!=std::string::npos);
  assert(exact.find("\"style\"")==exact.rfind("\"style\""));
  // Suitecut applies no node, string, document or depth cap of its own: a DOM
  // chain deeper than the former 512 recursion limit materializes whole.
  {constexpr size_t kDepth=5000;std::string types="[9",names="[2",values="[4",parents="[-1",attrs="[[]";for(size_t i=1;i<kDepth;++i){types+=",1";names+=",3";values+=",4";parents+=","+std::to_string(i-1);attrs+=",[]";}
   auto deep=CefParseJSON(R"({"strings":["frame-a","https://example.test/","#document","DIV",""],"documents":[{"frameId":0,"documentURL":1,"nodes":{"nodeType":)"+types+"],\"nodeName\":"+names+"],\"nodeValue\":"+values+"],\"parentIndex\":"+parents+"],\"attributes\":"+attrs+"]}}]}",JSON_PARSER_RFC);
   assert(deep&&tree.Convert(deep->GetDictionary(),{},docs,error));size_t divs=0;for(size_t at=docs[0].html_json.find("\"DIV\"");at!=std::string::npos;at=docs[0].html_json.find("\"DIV\"",at+1))++divs;assert(divs==kDepth-1);}

}
