#pragma once
#include <JitCpp/JitPlatform.hpp>
#include <JitCpp/JitUtils.hpp>
#include <clang/Frontend/TextDiagnosticBuffer.h>

#include <vector>
#include <string>
#include <sstream>
extern int cc1_main(llvm::ArrayRef<const char*> Argv, const char* Argv0, void* MainAddr, clang::DiagnosticConsumer*);

namespace Jit
{

class ClangCC1Driver
{
public:
  ClangCC1Driver() = default;

  // As long as the driver exists, source files remain on disk to allow
  // debugging JITed code.
  ~ClangCC1Driver();


  llvm::Expected<std::unique_ptr<llvm::Module>>
  compileTranslationUnit(const std::string& cppCode, const std::vector<std::string>& flags, llvm::LLVMContext& context);


private:
  //! Default compiler arguments
  static std::vector<std::string>
  getClangCC1Args(llvm::StringRef cpp, llvm::StringRef bc);

  //! Actual invocation of clang
  static llvm::Error compileCppToBitcodeFile(const std::vector<std::string>& args);


  std::vector<std::function<void()>> m_deleters;
};


}
