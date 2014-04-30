#ifndef CLING_TAG_MANAGER_H
#define CLING_TAG_MANAGER_H
#include <vector>
#include <map>
#include "cling/Interpreter/Interpreter.h"
#include "cling/CtagsExtension/Wrapper.h"
namespace cling {
  class TagManager
  {
  public:
    TagManager();
    
    class LookupInfo{
    public:
      LookupInfo(std::string h,std::string n,std::string t);
      std::string header;
      std::string name;
      std::string type;
    };
    
    void AddTagFile(llvm::StringRef path);
    typedef std::multimap<std::string,LookupInfo> TableType;
    TableType::iterator begin(std::string name);
    TableType::iterator end(std::string name);

  private:
    std::vector<TagFileWrapper> tags;
    //cling::Interpreter* ip;
    TableType table;
    
  };
}
#endif
