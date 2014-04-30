#ifndef CLING_CTAGS_WRAPPER_H
#define CLING_CTAGS_WRAPPER_H
// #include "readtags.h"
#include<vector>
#include<cstdlib>
#include<map>
#include<string>

namespace cling {
  struct TagFileInternals;
  
  class TagFileWrapper{
  public:
    TagFileWrapper(std::string path);
    TagFileWrapper(const std::vector<std::string>& file_list);
    ~TagFileWrapper();
    struct LookupResult
    {
        std::string name;
        std::string kind;
    };
    
    //returns a map that maps headers to lookup results 
    std::map<std::string,LookupResult> match(std::string name, bool partialMatch=false);
    bool newFile(){return generated;}
    bool validFile(){return validfile;}
  private:
    void generate(const std::vector<std::string>& cmd, std::string tagfile="adhoc", int arglimit=50);
    void read();
//     tagFile* tf;
//     tagFileInfo* tfi;
    TagFileInternals* tf;
    std::string tagfilename;
    std::string tagpath;
    static int counter;
    bool generated;
    bool validfile;
  };
}
#endif
