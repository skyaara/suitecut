// SPDX-License-Identifier: MIT
// Seeded projection/diff properties; no browser, network or task grader.
#include "suitecut/observation.hpp"
#include "suitecut/agent_tools.hpp"
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>
using suitecut::Json;
int main() {
  try {
    const auto iterations=std::getenv("SUITECUT_AGENT_FUZZ_ITERATIONS")?
      std::stoull(std::getenv("SUITECUT_AGENT_FUZZ_ITERATIONS")):1000;
    std::mt19937_64 random(0x4147454e545549ULL);
    std::size_t projected=0;
    for(std::size_t iteration=0;iteration<iterations;++iteration) {
      Json::array forest;std::vector<std::string> secrets;
      auto generate=[&](auto&& self,int depth)->Json {
        static const char* roles[]={"button","generic","textbox","heading","checkbox","text","slider","password"};
        const auto role=roles[random()%8];
        Json node{{"role",role},{"name","Control "+std::to_string(random()%24)},
          {"ref","native_"+std::to_string(projected++)},{"focusable",random()%3==0},
          {"disabled",random()%4==0},{"readOnly",random()%4==0}};
        if(random()%4==0)node["cursor"]="pointer";
        if(std::string(role)=="password") {
          const auto secret="secret_"+std::to_string(projected)+"_end";
          node["value"]=secret;node["text"]=secret;node["inputType"]="password";
          secrets.push_back(secret);
        } else if(random()%3==0)node["placeholder"]="Unicode λ 😀 "+std::string(random()%80,'x');
        if(depth<5&&random()%3==0) {
          Json::array children;
          for(std::size_t i=0,count=random()%5;i<count;++i)children.push_back(self(self,depth+1));
          node["children"]=std::move(children);
        }
        return node;
      };
      for(std::size_t i=0,count=random()%20;i<count;++i)forest.push_back(generate(generate,0));
      suitecut::ObservationOptions options;
      options.interactive_only=random()%2;options.max_nodes=random()%40;
      options.max_text_bytes=random()%4096;options.max_scan_nodes=random()%150;
      auto observation=suitecut::compact_observation(forest,options);
      if(observation.at("nodes").as_array().size()>options.max_nodes||
         static_cast<std::size_t>(observation.at("textBytes").as_int())>options.max_text_bytes||
         static_cast<std::size_t>(observation.at("scannedNodes").as_int())>options.max_scan_nodes)
        throw std::runtime_error("Observation exceeded a declared budget");
      const auto serialized=observation.dump();
      if(Json::parse(serialized)!=observation)throw std::runtime_error("Projection roundtrip failed");
      for(const auto& secret:secrets)if(serialized.find(secret)!=std::string::npos)
        throw std::runtime_error("Protected value leaked from projection");
      auto refreshed=observation;
      for(auto& node:refreshed["nodes"].as_array())if(node.contains("ref"))node["ref"]="fresh_"+node.at("ref").as_string();
      const auto diff=suitecut::observation_diff(observation,refreshed,random()%50);
      if(!diff.at("changed").as_array().empty())throw std::runtime_error("Ref refresh became a semantic change");
      if((observation.at("truncated").as_bool())&&!diff.at("removalsSuppressed").as_bool())
        throw std::runtime_error("Incomplete projection inferred removals");
    }
    for(const auto* profile:{"core","network,state","debug","media","all"})
      if(suitecut::agent_tool_catalog(profile).as_array().empty())throw std::runtime_error("Empty capability catalogue");
    std::cout<<"{\"iterations\":"<<iterations<<",\"seed\":\"4147454e545549\",\"nodesGenerated\":"<<projected<<",\"ok\":true}\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
