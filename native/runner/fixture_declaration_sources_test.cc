// SPDX-License-Identifier: MIT
#include "fixture_declaration_sources.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <unistd.h>
using namespace suitecut::runner;
static void Write(const std::string& file,const std::string& text){std::ofstream out(file);out<<text;assert(out.good());}
int main(){char name[]="/tmp/suitecut-fixture-source-XXXXXX";const auto* root=::mkdtemp(name);assert(root);const auto file=std::string(root)+"/source.mjs";unsigned n=0;
 {Write(file,"line one\nconst actual = 'λ';\n");FixtureDeclarationSources sources;sources.Capture({1,file,2,1});assert(sources.Excerpt(1)=="> 2 | const actual = 'λ';"&&sources.Location(1)->declaration==1);++n;}
 {FixtureDeclarationSources sources;sources.Capture({2,file,99,1});assert(!sources.Excerpt(2)&&!sources.Excerpt(1));++n;}
 {FixtureDeclarationSources sources;sources.Capture({3,file,1,1});Write(file,"actually replaced content\n");assert(!sources.Excerpt(3));++n;}
 {FixtureDeclarationSources sources;sources.Capture({4,file,1,1});assert(::rename(file.c_str(),(file+".old").c_str())==0);Write(file,"different inode\n");assert(!sources.Excerpt(4));++n;}
 {FixtureDeclarationSources sources;sources.Capture({5,file+".missing",1,1});assert(!sources.Excerpt(5)&&sources.Location(5));++n;}
 {FixtureDeclarationSources sources;Write(file,std::string(2049,'x'));sources.Capture({6,file,1,1});assert(!sources.Excerpt(6));++n;}
 {FixtureDeclarationSources sources;bool rejected=false;try{sources.Capture({0,file,1,1});}catch(const std::invalid_argument&){rejected=true;}assert(rejected);++n;}
 {FixtureDeclarationSources sources;sources.Capture({7,file,1,1});bool rejected=false;try{sources.Capture({7,file,2,1});}catch(const std::invalid_argument&){rejected=true;}assert(rejected);++n;}
 std::filesystem::remove_all(root);assert(n==8);std::cout<<"PASS "<<n<<" genuine native fixture declaration source controls\n";
}
