// innoextract runs other programs only for GOG.com installers (unrar), which
// TribesPad never asks for; iOS cannot start processes. In place of util/process.cpp.
#include "util/process.hpp"
namespace util { int run(const char * const args[]) { (void)args; return -1; } }
