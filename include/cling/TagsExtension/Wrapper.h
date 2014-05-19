#ifndef CLING_CTAGS_WRAPPER_H
#define CLING_CTAGS_WRAPPER_H
// #include "readtags.h"
#include<vector>
#include<cstdlib>
#include<map>
#include<string>

namespace cling{
  class TagFileWrapper{
  public:
    struct LookupResult{
      std::string name;
      std::string kind;
    };
    virtual std::map<std::string,LookupResult> match(std::string name, bool partialMatch=false)=0;
    virtual bool newFile()=0;
    virtual bool validFile()=0;
    virtual ~TagFileWrapper(){}
  };
}


#endif
