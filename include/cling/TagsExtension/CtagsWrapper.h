#include "Wrapper.h"
namespace cling {
  struct TagFileInternals;

  class CtagsFileWrapper:public TagFileWrapper{
  public:
    CtagsFileWrapper(std::string path, bool recurse=true);
    //If recurse is true, the files will be tagged recursively without preprocessing,
    //else only the files in 'path'(not in its subdirs)
    //will be tagged (after being preprocessed)
    CtagsFileWrapper(const std::vector<std::string>& file_list);
    ~CtagsFileWrapper(){}


    //returns a map that maps headers to lookup results
    std::map<std::string,LookupResult> match(std::string name, bool partialMatch=false);
    bool newFile(){return generated;}
    bool validFile(){return validfile;}
    bool operator==(const CtagsFileWrapper& t);

  private:
    void generate(const std::vector<std::string>& cmd, std::string tagfile="adhoc", int arglimit=50);
    void read();
//     tagFile* tf;
//     tagFileInfo* tfi;
    TagFileInternals* tf;
    std::string tagfilename;
    std::string tagpath;
    bool generated;
    bool validfile;
  };
}
