#include "Wrapper.h"
#include "readtags.h"
namespace cling {
  ///\brief Implements tag operations for Ctags
  class CtagsFileWrapper:public TagFileWrapper {
  public:
    CtagsFileWrapper(std::string path, bool recurse=true,bool fileP=false);

    ~CtagsFileWrapper(){}

    std::map<std::string,LookupResult> match
        (std::string name, bool partialMatch=false);

    bool newFile(){return m_Generated;}

    bool validFile(){return m_Validfile;}

  private:
    void generate(const std::vector<std::string>& cmd,
                  std::string tagfile="adhoc");
    void generate(std::string file);

    void read();

    struct TagFileInternals{
      tagFile* tf;
      tagFileInfo tfi;
    };

    TagFileInternals m_Tagfile;
    std::string m_Tagfilename;
    std::string m_Tagpath;
    bool m_Generated;
    bool m_Validfile;
  };
}//end namespace cling
