// SPDX-License-Identifier: MIT
#include "browser_profiles.h"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include "include/cef_api_hash.h"

int main() {
  cef_api_hash(CEF_API_VERSION, 0);
  char pattern[] = "/tmp/suitecut-profiles-XXXXXX";
  const std::filesystem::path root = mkdtemp(pattern);
  {  // A new install has one default profile and remembers it.
    suitecut::BrowserProfiles profiles(root);
    profiles.Load();
    assert(profiles.profiles().size() == 1 && profiles.profiles()[0].dir == "Default");
    assert(profiles.last() == "Default");
    const auto work = profiles.Add("  Work\nstuff ");
    assert(work && work->dir == "Profile 2" && work->name == "Work stuff");
    const auto unnamed = profiles.Add("   ");
    assert(unnamed && unnamed->dir == "Profile 3" && unnamed->name == "Person 3");
    profiles.SetLast("Profile 2");
    profiles.SetLast("../outside");
    assert(profiles.last() == "Profile 2");
    assert(profiles.Rename("Default", "Personal") && !profiles.Rename("Profile 9", "x"));
    assert(profiles.Save());
  }
  {  // Names, directories and the last profile survive a restart.
    suitecut::BrowserProfiles profiles(root);
    profiles.Load();
    assert(profiles.profiles().size() == 3 && profiles.last() == "Profile 2");
    assert(profiles.Find("Default")->name == "Personal" && profiles.Find("Profile 3"));
    assert(profiles.PathFor("Profile 2") == root / "Profile 2");
  }
  {  // An existing directory on disk is never handed to a new profile.
    std::filesystem::create_directories(root / "Profile 4");
    suitecut::BrowserProfiles profiles(root);
    profiles.Load();
    assert(profiles.Add("Next")->dir == "Profile 5");
  }
  {  // A tampered registry cannot point outside the root or drop the default.
    std::ofstream(root / suitecut::BrowserProfiles::kFile)
        << R"({"profiles":[{"dir":"../../etc","name":"x"},{"dir":"Profile 2","name":"Work"},)"
           R"({"dir":"Profile 2","name":"dupe"},{"dir":"Profile 02","name":"y"}],"last":"../../etc"})";
    suitecut::BrowserProfiles profiles(root);
    profiles.Load();
    assert(profiles.profiles().size() == 2 && profiles.profiles()[0].dir == "Default");
    assert(profiles.profiles()[1].name == "Work" && profiles.last() == "Default");
  }
  {  // Corrupt JSON starts over with just the default profile.
    std::ofstream(root / suitecut::BrowserProfiles::kFile) << "{not json";
    suitecut::BrowserProfiles profiles(root);
    profiles.Load();
    assert(profiles.profiles().size() == 1 && profiles.last() == "Default");
  }
  {  // Long names are cut at a UTF-8 boundary.
    suitecut::BrowserProfiles profiles(root);
    profiles.Load();
    std::string name(63, 'a');
    name += "\xC3\xA9tail";
    assert(profiles.Add(name)->name == std::string(63, 'a'));
  }
  std::filesystem::remove_all(root);
  std::cout << "PASS browser profiles registry\n";
}
