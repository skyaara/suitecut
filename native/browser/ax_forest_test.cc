// SPDX-License-Identifier: MIT
// Native AX protocol projection and CEF value ownership regression checks.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "ax_forest.h"
#include "include/cef_api_hash.h"
#include <cassert>
#include <iostream>
int main(){
  cef_api_hash(CEF_API_VERSION,0);
  const auto raw=R"JSON({"nodes":[
    {"nodeId":"root","backendDOMNodeId":1,"ignored":true,"role":{"value":"generic"},"childIds":["list","button","input","link","hidden"]},
    {"nodeId":"list","backendDOMNodeId":2,"role":{"value":"list"},"childIds":["item"]},
    {"nodeId":"item","role":{"value":"listitem"},"childIds":["mark","span"]},
    {"nodeId":"mark","role":{"value":"ListMarker"},"name":{"value":"•"}},
    {"nodeId":"span","ignored":true,"role":{"value":"generic"},"childIds":["first","second"]},
    {"nodeId":"first","role":{"value":"StaticText"},"name":{"value":"Database "}},
    {"nodeId":"second","role":{"value":"StaticText"},"name":{"value":"initialised."}},
    {"nodeId":"button","role":{"value":"button"},"name":{"value":"Submit"},"childIds":["buttontext"],"properties":[{"name":"pressed","value":{"value":"mixed"}},{"name":"disabled","value":{"value":true}}]},
    {"nodeId":"buttontext","role":{"value":"StaticText"},"name":{"value":"Submit"}},
    {"nodeId":"input","role":{"value":"textbox"},"name":{"value":"Name"},"value":{"value":"current value"},"childIds":["inputtext"]},
    {"nodeId":"inputtext","role":{"value":"StaticText"},"name":{"value":"current value"}},
    {"nodeId":"link","backendDOMNodeId":7,"role":{"value":"link"},"name":{"value":"Link"},"properties":[{"name":"url","value":{"value":"https://example.com/path"}}]},
    {"nodeId":"hidden","ignored":true,"role":{"value":"none"},"ignoredReasons":[{"name":"ariaHiddenSubtree"}],"childIds":["hiddentext"]},
    {"nodeId":"hiddentext","role":{"value":"StaticText"},"name":{"value":"Hidden"}}
  ]})JSON";
  auto parsed=CefParseJSON(raw,JSON_PARSER_RFC);auto parent=parsed->GetDictionary();auto nodes=parent->GetList("nodes");
  SuiteCutAXForest projector;CefRefPtr<CefListValue> forest;std::vector<SuiteCutAXForest::Link> links;std::string error;
  assert(projector.Project(nodes,1,forest,links,error));assert(forest->GetSize()==4);
  auto list=forest->GetDictionary(0);assert(list->GetString("role")=="list");
  auto text=list->GetList("children")->GetDictionary(0)->GetList("children")->GetDictionary(0);assert(text->GetString("text")=="Database initialised.");
  auto button=forest->GetDictionary(1);assert(button->GetList("children")->GetSize()==0);assert(button->GetDictionary("states")->GetString("pressed")=="mixed");assert(button->GetDictionary("states")->GetBool("disabled"));
  assert(forest->GetDictionary(2)->GetString("text")=="current value");assert(forest->GetDictionary(2)->GetList("children")->GetSize()==0);
  assert(links.size()==1&&links[0].backend==7);assert(links[0].node->IsValid());links[0].node->GetDictionary("properties")->SetString("url","/path");assert(forest->GetDictionary(3)->GetDictionary("properties")->GetString("url")=="/path");
  // CEF adopts fresh dictionaries. Populate URL before attachment and verify
  // the actual projected child, rather than a detached factory reference.
  links[0].node->Remove("properties");SuiteCutAXForest::SetRawURL(links[0].node,"../raw?x=1#spot");
  assert(forest->GetDictionary(3)->GetDictionary("properties")->GetString("url")=="../raw?x=1#spot");
  SuiteCutAXForest::SetRawURL(links[0].node,"/updated");assert(forest->GetDictionary(3)->GetDictionary("properties")->GetString("url")=="/updated");
  auto image_input=CefParseJSON(R"JSON([{"nodeId":"image","backendDOMNodeId":9,"role":{"value":"image"},"name":{"value":"Native image"}}])JSON",JSON_PARSER_RFC);
  SuiteCutAXForest image_projector;CefRefPtr<CefListValue> image_forest;std::vector<SuiteCutAXForest::Link> image_links;
  assert(image_projector.Project(image_input->GetList(),9,image_forest,image_links,error));assert(image_forest->GetDictionary(0)->GetString("role")=="img");
  SuiteCutAXForest scoped;CefRefPtr<CefListValue> scoped_forest;std::vector<SuiteCutAXForest::Link> scoped_links;assert(scoped.Project(nodes,2,scoped_forest,scoped_links,error));assert(scoped_forest->GetSize()==1);
  // The projection owns its output independently of the captured CEF graph.
  parent=nullptr;parsed=nullptr;nodes=nullptr;
  assert(forest->GetDictionary(0)->GetString("role")=="list");
  auto check_failure=[](const std::string& json,const char* message){
    auto input=CefParseJSON(json,JSON_PARSER_RFC);auto graph=input->GetList();
    SuiteCutAXForest bounded;CefRefPtr<CefListValue> output;std::vector<SuiteCutAXForest::Link> targets;std::string reason;
    assert(!bounded.Project(graph,1,output,targets,reason));assert(reason.find(message)!=std::string::npos);
  };
  check_failure(R"JSON([{"nodeId":"root","backendDOMNodeId":1,"role":{"value":"generic"},"childIds":["root"]}])JSON","cycle");
  check_failure(R"JSON([{"nodeId":"root","backendDOMNodeId":1,"role":{"value":"list"},"childIds":[42]}])JSON","child identity");
  check_failure("[{\"nodeId\":\"root\",\"backendDOMNodeId\":1,\"role\":{\"value\":\"button\"},\"name\":{\"value\":\""+std::string(70000,'x')+"\"}}]","64 KiB");
  auto deep=CefListValue::Create();
  for(int i=0;i<65;i++){auto node=CefDictionaryValue::Create();node->SetString("nodeId",std::to_string(i));if(!i)node->SetInt("backendDOMNodeId",1);auto role=CefDictionaryValue::Create();role->SetString("value","generic");node->SetDictionary("role",role);if(i<64){auto ids=CefListValue::Create();ids->SetString(0,std::to_string(i+1));node->SetList("childIds",ids);}deep->SetDictionary(i,node);}
  SuiteCutAXForest depth;CefRefPtr<CefListValue> unused;std::vector<SuiteCutAXForest::Link> unused_links;std::string depth_error;
  assert(!depth.Project(deep,1,unused,unused_links,depth_error));assert(depth_error.find("depth limit")!=std::string::npos);
  assert(SuiteCutAXForest::Normalize(" a \xc2\xa0 b\xe2\x80\x8b ")=="a b");
  {
  const auto project=[](const std::string& json){auto raw=CefParseJSON(json,JSON_PARSER_RFC);SuiteCutAXForest projector;CefRefPtr<CefListValue> forest;std::vector<SuiteCutAXForest::Link> links;std::string error;assert(projector.Project(raw->GetList(),1,forest,links,error));return forest;};
  const auto tree=R"JSON([
    {"nodeId":"root","backendDOMNodeId":1,"role":{"value":"generic"},"childIds":["a","b"]},
    {"nodeId":"a","role":{"value":"generic"},"__nativeLayout":{"block":true},"childIds":["t1"]},
    {"nodeId":"b","role":{"value":"generic"},"__nativeLayout":{"block":true},"childIds":["t2"]},
    {"nodeId":"t1","role":{"value":"StaticText"},"name":{"value":"One"},"__nativeLayout":{"box":[10,20,30,10]}},
    {"nodeId":"t2","role":{"value":"StaticText"},"name":{"value":"Two"},"__nativeLayout":{"box":[5,40,20,15]}}
  ])JSON";
  auto forest=project(tree);auto text=forest->GetDictionary(0);assert(text->GetString("text")=="One Two");auto box=text->GetList("box");assert(box&&box->GetSize()==4&&box->GetDouble(0)==5&&box->GetDouble(1)==20&&box->GetDouble(2)==35&&box->GetDouble(3)==35);
  auto inline_tree=std::string(tree);size_t p=0;while((p=inline_tree.find("\"block\":true",p))!=std::string::npos){inline_tree.replace(p,12,"\"block\":false");p+=13;}assert(project(inline_tree)->GetDictionary(0)->GetString("text")=="OneTwo");
  auto details=project(R"JSON([{"nodeId":"root","backendDOMNodeId":1,"role":{"value":"group"},"childIds":["summary"]},{"nodeId":"summary","role":{"value":"DisclosureTriangle"},"name":{"value":"Summary"},"childIds":["text"]},{"nodeId":"text","role":{"value":"StaticText"},"name":{"value":"Summary"}}])JSON");assert(details->GetDictionary(0)->GetString("role")=="group");assert(details->GetDictionary(0)->GetList("children")->GetDictionary(0)->GetString("text")=="Summary");
  assert(project(R"JSON([{"nodeId":"root","backendDOMNodeId":1,"role":{"value":"generic"},"__nativeLayout":{"hidden":true},"childIds":["button"]},{"nodeId":"button","role":{"value":"button"},"name":{"value":"Visible child"}}])JSON")->GetSize()==0);
  assert(project(R"JSON([{"nodeId":"root","backendDOMNodeId":1,"role":{"value":"textbox"},"properties":[{"name":"invalid","value":{"value":"grammar"}}]}])JSON")->GetDictionary(0)->GetDictionary("states")->GetString("invalid")=="grammar");
  assert(project(R"JSON([{"nodeId":"root","backendDOMNodeId":1,"role":{"value":"textbox"},"properties":[{"name":"invalid","value":{"value":"true"}}]}])JSON")->GetDictionary(0)->GetDictionary("states")->GetBool("invalid"));

  }
  {
    auto raw=CefParseJSON(R"JSON([{"nodeId":"root","backendDOMNodeId":1,"role":{"value":"textbox"},"name":{"value":"Label"},"__nativeLayout":{"placeholder":"Actual placeholder","invalidToken":"grammar"},"properties":[{"name":"invalid","value":{"value":true}}]}])JSON",JSON_PARSER_RFC);
    SuiteCutAXForest captured;CefRefPtr<CefListValue> output;std::vector<SuiteCutAXForest::Link> targets;std::string reason;
    assert(captured.Project(raw->GetList(),1,output,targets,reason));auto node=output->GetDictionary(0);assert(node->GetDictionary("states")->GetBool("invalid"));assert(node->GetString("invalidToken")=="grammar");assert(node->GetDictionary("properties")->GetString("placeholder")=="Actual placeholder");
    raw=CefParseJSON(R"JSON([{"nodeId":"root","backendDOMNodeId":1,"role":{"value":"textbox"},"name":{"value":"Placeholder"},"__nativeLayout":{"placeholder":"Placeholder","invalidToken":"spelling"},"properties":[{"name":"invalid","value":{"value":false}}]}])JSON",JSON_PARSER_RFC);
    SuiteCutAXForest named;assert(named.Project(raw->GetList(),1,output,targets,reason));node=output->GetDictionary(0);assert(node->GetDictionary("states")->HasKey("invalid")&&!node->GetDictionary("states")->GetBool("invalid"));assert(node->GetString("invalidToken")=="spelling");assert(!node->HasKey("properties"));
  }
  auto value=CefValue::Create();value->SetList(forest);std::cout<<CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString()<<"\n";
}
