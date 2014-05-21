#include "cling/TagsExtension/TagManager.h"
#include "cling/TagsExtension/CtagsWrapper.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/FileSystem.h"
#include <algorithm>

namespace cling {

  TagManager::TagManager(){}
  void TagManager::AddTagFile(llvm::StringRef path, bool recurse){

    if (llvm::sys::path::is_relative(path)) {
      llvm::SmallString<100> str(path.data());
      llvm::error_code ec=llvm::sys::fs::make_absolute(str);
      if (ec!=llvm::errc::success)
        llvm::errs()<<"Can't deduce absolute path.\n";
      else
        path=str.c_str();
    }

    auto tf=new CtagsFileWrapper(path,recurse);
    if (!tf->validFile()) {
      llvm::errs() << "Reading Tag File: " << path << " failed.\n";
      return;
    }
    if (std::find(m_Tags.begin(), m_Tags.end(), tf)==m_Tags.end())
      m_Tags.push_back(tf);
  }

  TagManager::TableType::iterator
  TagManager::begin(std::string name){
    m_Table.erase(name);
    for (auto& t:m_Tags){
      for (auto match:t->match(name, true)){
        LookupInfo l(match.first, match.second.name, match.second.kind);
        m_Table.insert({name, l});
      }
    }
    auto r=m_Table.equal_range(name);
    return r.first;
  }
  TagManager::TableType::iterator
  TagManager::end(std::string name){
    auto r=m_Table.equal_range(name);
    return r.second;
  }
  TagManager::LookupInfo::LookupInfo
    (std::string h, std::string n, std::string t):
      header(h), name(n), type(t){}

} //end namespace cling
