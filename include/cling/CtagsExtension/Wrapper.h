#ifndef CLING_CTAGS_WRAPPER_H
#define CLING_CTAGS_WRAPPER_H


#include "readtags.h"
#include<vector>
#include<cstdlib>
#include<map>
#include<string>

namespace cling {
  class TagFileWrapper{
  public:
    TagFileWrapper(std::string path);
    TagFileWrapper(const std::vector<std::string>& file_list);
    
    struct LookupResult
    {
        std::string name;
        std::string kind;
    };
    
    //returns a map that maps headers to lookup results 
    std::map<std::string,LookupResult> match(std::string name, bool partialMatch=false);
private:
    void generate(const std::vector<std::string>& cmd,int arglimit=50);
    bool read();
    tagFile* tf;
    tagFileInfo tfi;
    std::string tagfilename;
      
  };
}
#endif