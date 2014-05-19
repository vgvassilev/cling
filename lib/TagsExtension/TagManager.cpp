#include "cling/TagsExtension/TagManager.h"
#include <llvm/Support/raw_ostream.h>
#include <algorithm>
#include "cling/TagsExtension/CtagsWrapper.h"
namespace cling {

  TagManager::TagManager(){}
  void TagManager::AddTagFile(llvm::StringRef path, bool recurse){
    auto tf=new CtagsFileWrapper(path,recurse);
    if(!tf->validFile())
    {
      llvm::errs()<<"Reading Tag File: "<<path<<" failed.\n";
      return;
    }
    if(std::find(m_tags.begin(),m_tags.end(),tf)==m_tags.end())
      m_tags.push_back(tf);
  }

  TagManager::TableType::iterator
  TagManager::begin(std::string name){
    m_table.erase(name);
    for(auto& t:m_tags){
      for(auto match:t->match(name,true)){
        LookupInfo l(match.first,match.second.name,match.second.kind);
        m_table.insert({name,l});
      }
    }
    auto r=m_table.equal_range(name);
    return r.first;
  }
  TagManager::TableType::iterator
  TagManager::end(std::string name){
    auto r=m_table.equal_range(name);
    return r.second;
  }
  TagManager::LookupInfo::LookupInfo(std::string h, std::string n, std::string t):
      header(h),name(n),type(t){}

} //end namespace cling
