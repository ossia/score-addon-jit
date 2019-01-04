#pragma once
#include <JitCpp/JitPlatform.hpp>
#include <JitCpp/JitUtils.hpp>
#include <clang/Frontend/TextDiagnosticBuffer.h>

#include <vector>
#include <string>

namespace Jit
{

class ClangCC1Driver
{
public:
  ClangCC1Driver() = default;
  ~ClangCC1Driver();

  static QDir bitcodeDatabase();

  llvm::Expected<std::unique_ptr<llvm::Module>>
  compileTranslationUnit(const std::string& cppCode, const std::vector<std::string>& flags, llvm::LLVMContext& context);

private:
  //! Default compiler arguments
  static std::vector<std::string>
  getClangCC1Args();

  //! Actual invocation of clang
  static llvm::Error compileCppToBitcodeFile(const std::vector<std::string>& args);

  std::vector<std::function<void()>> m_deleters;
};


}
