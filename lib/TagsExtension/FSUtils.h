#ifndef CLING_FS_UTILS_H
#define CLING_FS_UTILS_H
#include <llvm/ADT/StringRef.h>
namespace cling {
    std::string path_to_file_name(std::string path);
    bool file_exists(std::string path);
    bool file_is_newer(std::string path,std::string dir);
    bool need_to_generate(std::string tagpath,std::string filename,std::string dirpath);
    std::string generate_tag_path();
    bool isHeaderFile(llvm::StringRef str);
    std::pair<std::string,std::string> splitPath(std::string path);
    void mkdir(std::string path);
    std::string get_home();
}
#endif
