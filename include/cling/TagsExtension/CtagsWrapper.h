#include "Wrapper.h"
namespace cling {
  struct TagFileInternals;

  class CtagsFileWrapper:public TagFileWrapper {
  public:
    CtagsFileWrapper(std::string path, bool recurse=true);
    //If recurse is true, the files will be tagged recursively without preprocessing,
    //else only the files in 'path'(not in its subdirs)
    //will be tagged (after being preprocessed)
    CtagsFileWrapper(const std::vector<std::string>& file_list);
    ~CtagsFileWrapper(){}


    //returns a map that maps headers to lookup results
    std::map<std::string,LookupResult> match(std::string name, bool partialMatch=false);
    bool newFile(){return m_generated;}
    bool validFile(){return m_validfile;}
    bool operator==(const CtagsFileWrapper& t);

  private:
    void generate(const std::vector<std::string>& cmd, std::string tagfile="adhoc", int argLimit=50);
    void read();
//     tagFile* tf;
//     tagFileInfo* tfi;
    TagFileInternals* m_tf;
    std::string m_tagfilename;
    std::string m_tagpath;
    bool m_generated;
    bool m_validfile;
  };
}//end namespace cling
