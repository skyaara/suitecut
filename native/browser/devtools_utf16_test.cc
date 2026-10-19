#include "include/cef_api_hash.h"
#include "devtools_utf16.h"
#include "inbound_string.h"
#include "ax_forest.h"
#include <cassert>
#include <iostream>
int main(){cef_api_hash(CEF_API_VERSION,0);
 const std::string source=R"({"id":37,"result":{"nodes":[{"name":{"type":"string","value":"\ud800a\u0000\\\"\udfff\ud83d\ude00"}}],"literal":"__suitecutNativeUTF16Scalar","object":{"__suitecutNativeUTF16Scalar":"a string"}}})";
 std::string projected,error;assert(suitecut::DevToolsLosslessProjection(source,projected,error));assert(source.find("\\ud800")!=std::string::npos);
 const auto parsed=CefParseJSON(projected,JSON_PARSER_RFC);assert(parsed);auto root=parsed->GetDictionary();assert(root->GetInt("id")==37);
 auto result=root->GetDictionary("result");auto value=result->GetList("nodes")->GetDictionary(0)->GetDictionary("name")->GetValue("value");std::u16string actual;assert(suitecut::DevToolsReadTag(value,actual));
 const std::u16string expected={0xd800,u'a',0,u'\\',u'"',0xdfff,0xd83d,0xde00};assert(actual==expected);
 auto forest=CefDictionaryValue::Create();suitecut::DevToolsSetString(forest,"name",suitecut::DevToolsString(value));auto forest_value=CefValue::Create();forest_value->SetDictionary(forest);std::string json;assert(suitecut::DevToolsLosslessJSON(forest_value,json));assert(json=="{\"name\":"+suitecut::DevToolsQuoteUnits(expected)+"}");
 assert(result->GetString("literal")==suitecut::kDevToolsUTF16Tag);assert(result->GetDictionary("object")->GetString(suitecut::kDevToolsUTF16Tag)=="a string");
 assert(!suitecut::DevToolsLosslessProjection(R"({"\ud800":1})",projected,error));assert(error.find("object key")!=std::string::npos);
 assert(!suitecut::DevToolsLosslessProjection(R"({"id":37,"value":"\ud800",]})",projected,error));
 const std::string pair=R"({"value":"\ud83d\ude00"})";assert(suitecut::DevToolsLosslessProjection(pair,projected,error));assert(pair==projected);
 auto author=CefValue::Create();author->SetString("{\"__suitecutNativeUTF16Scalar\":\"d800\"}");json.clear();assert(suitecut::DevToolsLosslessJSON(author,json));assert(json==CefWriteJSON(author,JSON_WRITER_DEFAULT).ToString());
 assert(!suitecut::DevToolsLosslessProjection("{\"value\":\""+std::string(1048577,'a')+"\\ud800\"}",projected,error));
 const std::string ax=R"({"nodes":[{"nodeId":"1","backendDOMNodeId":9,"role":{"type":"role","value":"heading"},"name":{"type":"string","value":"  \ud800\u00a0name  "},"childIds":["2"]},{"nodeId":"2","role":{"type":"role","value":"StaticText"},"name":{"type":"string","value":"  \ud800\u00a0name  "}}]})";
 assert(suitecut::DevToolsLosslessProjection(ax,projected,error));auto ax_parse=CefParseJSON(projected,JSON_PARSER_RFC);SuiteCutAXForest projector;CefRefPtr<CefListValue> output;std::vector<SuiteCutAXForest::Link> links;assert(projector.Project(ax_parse->GetDictionary()->GetList("nodes"),9,output,links,error));
 auto ax_out=CefValue::Create();ax_out->SetList(output->Copy());json.clear();assert(suitecut::DevToolsLosslessJSON(ax_out,json));assert(json.find("\\ud800")!=std::string::npos);assert(json.find(suitecut::kDevToolsUTF16Tag)==std::string::npos);assert(output->GetDictionary(0)->GetList("children")->GetSize()==0);
 std::u16string normal_name;assert(suitecut::DevToolsReadTag(output->GetDictionary(0)->GetValue("name"),normal_name));assert(normal_name==std::u16string({0xd800,u' ',u'n',u'a',u'm',u'e'}));
 auto joined=CefDictionaryValue::Create();suitecut::DevToolsSetString(joined,"pair",suitecut::DevToolsUnitsWTF8({0xd83d})+suitecut::DevToolsUnitsWTF8({0xde00}));assert(joined->GetType("pair")==VTYPE_STRING);assert(joined->GetString("pair").ToString()==suitecut::DevToolsUnitsWTF8({0xd83d,0xde00}));
 const auto isolated_text=suitecut::DevToolsUnitsWTF8({u'a',0xd800,u'b'});assert(suitecut::DevToolsTruncateWTF8(isolated_text,3)=="a");assert(suitecut::DevToolsTruncateWTF8(isolated_text,4)==suitecut::DevToolsUnitsWTF8({u'a',0xd800}));assert(suitecut::DevToolsTruncateWTF8(isolated_text.substr(0,3),3)=="a");
 auto console=CefDictionaryValue::Create();console->SetString("type","log");suitecut::DevToolsSetString(console,"text",suitecut::DevToolsUnitsWTF8(expected));auto console_value=CefValue::Create();console_value->SetDictionary(console);auto private_packet=CefWriteJSON(console_value,JSON_WRITER_DEFAULT);assert(CefParseJSON(private_packet,JSON_PARSER_RFC));json.clear();assert(suitecut::DevToolsLosslessJSON(console_value,json));assert(json.find(suitecut::kDevToolsUTF16Tag)==std::string::npos);assert(json.find("\\ud800")!=std::string::npos);
 using namespace suitecut;
 auto inbound=CefDictionaryValue::Create();inbound->SetString("nameJSON",DevToolsQuoteUnits(expected));std::string restored;
 assert(InboundString(inbound,"name",restored,256));assert(restored==DevToolsUnitsWTF8(expected));
 auto ingress_value=CefValue::Create();ingress_value->SetDictionary(inbound->Copy(false));const auto ingress_json=CefWriteJSON(ingress_value,JSON_WRITER_DEFAULT);const auto ingress_parsed=CefParseJSON(ingress_json,JSON_PARSER_RFC);assert(ingress_parsed);assert(InboundString(ingress_parsed->GetDictionary(),"name",restored,256));assert(restored==DevToolsUnitsWTF8(expected));
 assert(DevToolsStringUnits(InboundQuote(restored),actual));assert(actual==expected);
 auto event_name=CefDictionaryValue::Create();InboundSetString(event_name,"name",restored);assert(event_name->GetType("nameJSON")==VTYPE_STRING);assert(!event_name->HasKey("name"));assert(InboundString(event_name,"name",restored,256));assert(restored==DevToolsUnitsWTF8(expected));
 inbound->SetString("name","ordinary");assert(!InboundString(inbound,"name",restored,256));inbound->Remove("name");inbound->SetString("nameJSON","{\"__suitecutNativeUTF16Scalar\":\"d800\"}");assert(!InboundString(inbound,"name",restored,256));
 inbound->SetString("nameJSON","\"\\ud800\"");assert(!InboundString(inbound,"name",restored,0));inbound->SetString("nameJSON","\"unterminated");assert(!InboundString(inbound,"name",restored,256));
 auto ordinary=CefDictionaryValue::Create();ordinary->SetString("name","__suitecutNativeUTF16Scalar");assert(InboundString(ordinary,"name",restored,256));assert(restored=="__suitecutNativeUTF16Scalar");
 const std::string dom=R"({"node":{"nodeId":0,"backendNodeId":72,"nodeType":1,"nodeName":"BUTTON","localName":"button","nodeValue":"","attributes":["id","\ud800a\u0000\\\"\udfff\ud83d\ude00","data-author","{\"__suitecutNativeUTF16Scalar\":\"d800\"}"],"contentDocument":{"frameId":"actual-frame"}}})";
 assert(DevToolsLosslessProjection(dom,projected,error));auto dom_parsed=CefParseJSON(projected,JSON_PARSER_RFC);auto dom_node=dom_parsed->GetDictionary()->GetDictionary("node");assert(dom_node->GetInt("backendNodeId")==72);assert(dom_node->GetString("nodeName")=="BUTTON");assert(dom_node->GetDictionary("contentDocument")->GetString("frameId")=="actual-frame");assert(DevToolsReadTag(dom_node->GetList("attributes")->GetValue(1),actual));assert(actual==expected);assert(dom_node->GetList("attributes")->GetString(3)=="{\"__suitecutNativeUTF16Scalar\":\"d800\"}");
 const std::string resolved=R"({"object":{"type":"object","subtype":"node","objectId":"genuine-node-reference","description":"button#\ud800a\u0000\\\"\udfff\ud83d\ude00"}})";
 assert(DevToolsLosslessProjection(resolved,projected,error));auto resolve_parsed=CefParseJSON(projected,JSON_PARSER_RFC);auto remote_node=resolve_parsed->GetDictionary()->GetDictionary("object");assert(remote_node->GetString("objectId")=="genuine-node-reference");assert(DevToolsString(remote_node->GetValue("description"))=="button#"+DevToolsUnitsWTF8(expected));
 // A borrowed token is bounded by its explicit slice, not the backing
 // allocation or its trailing bytes. No view survives the synchronous decode.
 const std::string token_storage="prefix\"plain\\u0000\\ud800\\ud83d\\ude00\"ignored";
 const auto begin=token_storage.find('"');const auto end=token_storage.rfind('"');
 const std::string_view borrowed(token_storage.data()+begin,end-begin+1);
 assert(DevToolsStringUnits(borrowed,actual));assert(actual==std::u16string({u'p',u'l',u'a',u'i',u'n',0,0xd800,0xd83d,0xde00}));
 assert(!DevToolsStringUnits(borrowed.substr(0,borrowed.size()-1),actual));
 const std::string truncated_utf8="\"\xf0\x9f\x98\x80\"suffix";
 assert(!DevToolsStringUnits(std::string_view(truncated_utf8.data(),4),actual));
 assert(DevToolsStringUnits(std::string_view(truncated_utf8.data(),6),actual));assert(actual==std::u16string({0xd83d,0xde00}));
 // An ordinary large scalar stays byte-exact beside a separately tagged real
 // isolated scalar. Adjacent tokens and escaped quote/backslash boundaries
 // must remain independent when no token allocation separates their bytes.
 const std::string large=std::string(131072,'x')+"\\\"tail";
 const std::string mixed="{\"ordinary\":"+InboundQuote(large)+",\"isolated\":\"\\ud800\",\"empty\":\"\"}";
 assert(DevToolsLosslessProjection(mixed,projected,error));const auto mixed_value=CefParseJSON(projected,JSON_PARSER_RFC);assert(mixed_value);
 const auto mixed_dict=mixed_value->GetDictionary();assert(mixed_dict->GetString("ordinary").ToString()==large&&mixed_dict->GetString("empty").empty());
 assert(DevToolsReadTag(mixed_dict->GetValue("isolated"),actual)&&actual==std::u16string({0xd800}));
 auto output_text=CefDictionaryValue::Create();const std::string ordinary_text("a\0b",3);
 DevToolsSetString(output_text,"ordinary",ordinary_text);assert(output_text->GetType("ordinary")==VTYPE_STRING&&output_text->GetString("ordinary").ToString()==ordinary_text);
 DevToolsSetString(output_text,"paired",DevToolsUnitsWTF8({0xd83d,0xde00}));assert(output_text->GetType("paired")==VTYPE_STRING&&output_text->GetString("paired").ToString()==DevToolsUnitsWTF8({0xd83d,0xde00}));
 DevToolsSetString(output_text,"isolated",DevToolsUnitsWTF8({0xdfff}));assert(DevToolsReadTag(output_text->GetValue("isolated"),actual)&&actual==std::u16string({0xdfff}));
 std::cout<<"PASS actual CEF lossless private scalar sidecar: exact isolated/pair/NUL/backslash/quote units, no opaque caption tokens, malformed/key/budget errors, ordinary pair/string collision unchanged\n";
}
