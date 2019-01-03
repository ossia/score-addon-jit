#include <JitCpp/ClangDriver.hpp>

namespace Jit
{

ClangCC1Driver::~ClangCC1Driver()
{
  for (const auto& D : m_deleters)
    D();
}

llvm::Expected<std::unique_ptr<llvm::Module> > ClangCC1Driver::compileTranslationUnit(const std::string& cppCode, const std::vector<std::string>& flags, llvm::LLVMContext& context)
{
  auto sourceFileName = saveSourceFile(cppCode);
  if (!sourceFileName)
    return sourceFileName.takeError();

  std::string cpp = *sourceFileName;
  std::string preproc = replaceExtension(cpp, "preproc.cpp");
  std::string bc = replaceExtension(cpp, "bc");

  // Default flags
  auto flags_vec = getClangCC1Args(cpp, bc);
  flags_vec.push_back("-main-file-name");
  flags_vec.push_back(cpp);
  flags_vec.push_back("-x");
  flags_vec.push_back("c++");

  // Additional flags
  flags_vec.insert(flags_vec.end(), flags.begin(), flags.end());

  // First do a preprocessing pass that we will hash
  flags_vec.push_back("-E");
  flags_vec.push_back("-P");
  flags_vec.push_back("-o");
  flags_vec.push_back(preproc);
  flags_vec.push_back(cpp);

  {
    Timer t;
    llvm::Error err = compileCppToBitcodeFile(flags_vec);
    if (err)
      return std::move(err);
  }
  // THen if there isn't a matching bitcode file, do the actual build
  flags_vec.resize(flags_vec.size() - 5);
  flags_vec.push_back("-o");
  flags_vec.push_back(bc);
  flags_vec.push_back(cpp);

  {
    Timer t;
    llvm::Error err = compileCppToBitcodeFile(flags_vec);
    if (err)
      return std::move(err);
  }

  // Else...
  Timer t;
  auto module = readModuleFromBitcodeFile(bc, context);

  llvm::sys::fs::remove(bc);

  if (!module)
  {
    llvm::sys::fs::remove(cpp);
    return module.takeError();
  }

  m_deleters.push_back([cpp]() { llvm::sys::fs::remove(cpp); });

  return std::move(*module);
}

std::vector<std::string> ClangCC1Driver::getClangCC1Args(llvm::StringRef cpp, llvm::StringRef bc)
{
  std::vector<std::string> args;

  args.push_back("-emit-llvm");
  args.push_back("-emit-llvm-bc");
  args.push_back("-emit-llvm-uselists");

  populateIncludeDirs(args);
  populateCompileOptions(args);
  populateDefinitions(args);

  /*
    for(const auto& arg : args)
    {
      std::cerr << " -- " << arg << std::endl;
    }
    */

  return args;
}

llvm::Error ClangCC1Driver::compileCppToBitcodeFile(const std::vector<std::string>& args)
{
  std::vector<const char*> argsX; argsX.reserve(args.size());
  std::transform(
        args.begin(), args.end(), std::back_inserter(argsX),
        [](const std::string& s) { return s.c_str(); });

  auto diags = std::make_unique<clang::TextDiagnosticBuffer>();

  if (int res = cc1_main(argsX, "", nullptr, diags.get()))
  {
    std::stringstream ss;
    for(auto it = diags->err_begin(); it != diags->err_end(); ++it)
      ss << "error : " << it->second << "\n";
    return return_code_error(ss.str(), res);
  }

  return llvm::Error::success();
}

}
