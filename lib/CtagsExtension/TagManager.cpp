#include "cling/CtagsExtension/TagManager.h"
#include <iostream>
namespace cling {

  TagManager::TagManager(){}
  void TagManager::AddTagFile(llvm::StringRef path)
  {
    TagFileWrapper tf(path);
    if(!tf.emptyFile())
      tags.push_back(tf);
  }

  TagManager::TableType::iterator
  TagManager::begin(std::string name)
  {
      for(auto& t:tags)
      {
          for(auto match:t.match(name,true))
          {
              LookupInfo l(match.first,match.second.name,match.second.kind);
              table.insert({name,l});
          }
      }
      auto r=table.equal_range(name);
      return r.first;
  }
  TagManager::TableType::iterator
  TagManager::end(std::string name)
  {
      auto r=table.equal_range(name);
      return r.second;
  }
  TagManager::LookupInfo::LookupInfo(std::string h, std::string n, std::string t):
      header(h),name(n),type(t){}



}
