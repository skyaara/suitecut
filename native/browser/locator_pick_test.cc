// SPDX-License-Identifier: MIT
#include "tests/support/cef_library.h"
#include "element_bounds_geometry.h"
#include "element_bounds_reply.h"
#include "dictionary_number.h"
#define SUITECUT_LOCATOR_PICK_TEST_INCLUDE "controllers/dom/locator_pick_controller.inc"
#define SUITECUT_LOCATOR_SUGGESTION_TEST_NO_MAIN
#include "locator_suggestion_test.cc"
static void Pick(Harness&h,int x=13,int y=17,const std::string&frame="main"){
 assert(h.operations_.Begin(1,h.now,10));auto message=CefDictionaryValue::Create();message->SetInt("x",x);message->SetInt("y",y);message->SetString("frame",frame);h.BeginLocatorPick(1,message);
}
static void Hit(Harness&h,const std::string&frame="main"){
 h.Ack("DOM.getNodeForLocation","{\"frameId\":\""+frame+"\",\"backendNodeId\":913}");
 assert(h.calls.front().params->GetInt("backendNodeId")==913&&h.calls.front().params->GetInt("executionContextId")==h.documents.at(frame).utility);
 h.Ack("DOM.resolveNode",R"({"object":{"subtype":"node","objectId":"actual-native-hit-node"}})");
 h.Ack("Runtime.callFunctionOn",Capture(observed));
 h.Ack("Runtime.callFunctionOn",Capture(R"({"results":[{"matches":1,"same":true},{"matches":1,"same":true},{"matches":1,"same":true}]})"));
}
int main(){if(!LoadSuiteCutCEFForTest())return 1;assert(cef_api_hash(CEF_API_VERSION,0));int groups=0;
 {Harness h;Pick(h);assert(!h.queries&&h.calls.front().params->GetInt("x")==13&&!h.calls.front().params->HasKey("pierce"));Hit(h);const auto reply=Parse(h.responses.at(0));assert(reply->GetBool("pointPicking")&&reply->GetDictionary("provenance")->GetDictionary("point")->GetInt("inspectorY")==17);h.Drain();++groups;}
 {Harness h;h.documents["main"].session="realOOPIF";h.sessions_["realOOPIF"]="main";h.frame_states_["main"]={"outside-parent","realOOPIF"};Pick(h);Hit(h);assert(h.errors.empty()&&h.responses.size()==1);h.Drain();++groups;}
 {Harness h;h.documents["child"]={"child","","child-loader","child-main","child-utility",8,9,1};h.frame_states_["child"]={"main",""};Pick(h,20,30,"child");assert(h.calls.front().params->GetInt("contextId")==9);h.Ack("Runtime.evaluate",R"({"result":{"value":{"width":200,"height":100}}})");h.Ack("DOM.getFrameOwner",R"({"backendNodeId":77})");h.Ack("DOM.getBoxModel",R"({"model":{"content":[100,200,300,200,300,300,100,300]}})");assert(h.calls.front().params->GetInt("x")==120&&h.calls.front().params->GetInt("y")==230);Hit(h,"child");assert(h.errors.empty());const auto output=Parse(h.responses[0]);const auto point=output->GetDictionary("provenance")->GetDictionary("point");assert(point->GetInt("x")==20&&point->GetInt("inspectorX")==120&&point->GetString("inspectorRoot")=="main");h.Drain();++groups;}
 for(int kind=0;kind<4;++kind){Harness h;Pick(h);if(kind==0)h.Ack("DOM.getNodeForLocation",R"({"frameId":"other-frame","backendNodeId":913})");else if(kind==1)h.Ack("DOM.getNodeForLocation",R"({"backendNodeId":913})");else if(kind==2)h.Ack("DOM.getNodeForLocation",R"({"frameId":"main","backendNodeId":0})");else h.Ack("DOM.getNodeForLocation",R"({"message":"Backend method unavailable"})",false);assert(h.calls.empty()&&h.responses.empty()&&h.errors.size()==1&&!h.queries);h.Drain();++groups;}
 {Harness h;Pick(h);h.Ack("DOM.getNodeForLocation",R"({"frameId":"main","backendNodeId":913})");h.Ack("DOM.resolveNode",R"({"object":{"subtype":"node","objectId":"actual-native-hit-node"}})");h.Ack("Runtime.callFunctionOn",Capture(observed,914));assert(h.calls.empty()&&h.responses.empty()&&h.errors[0].find("picked backend")!=std::string::npos);h.Drain();++groups;}
 for(int kind=0;kind<3;++kind){Harness h;Pick(h);if(kind==0)++h.documents["main"].context_generation;else if(kind==1)h.frame_states_["main"].parent="replacement-parent";else h.now=10;h.Ack("DOM.getNodeForLocation",R"({"frameId":"main","backendNodeId":913})");assert(h.calls.empty()&&h.responses.empty()&&h.errors.size()==1);h.Drain();++groups;}
 {Harness h;Pick(h);h.Complete(1,false,"real controlled cancel");assert(h.selectors.PhysicalSize()==1);h.Ack("DOM.getNodeForLocation",R"({"frameId":"main","backendNodeId":913})");assert(h.calls.empty()&&h.responses.empty());h.Drain();assert(!h.selectors.PhysicalSize());++groups;}
 {Harness h;Pick(h,-1,2);assert(h.calls.empty()&&h.responses.empty()&&h.errors.size()==1);++groups;}
 std::cout<<"native locator picking "<<groups<<" controlled groups passed\n";
}
