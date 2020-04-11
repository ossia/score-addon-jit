
#include <QCryptographicHash>
#include <QStandardPaths>

#include <JitCpp/ClangDriver.hpp>

#include <sstream>

extern int cc1_main(
    llvm::ArrayRef<const char*> Argv,
    const char* Argv0,
    void* MainAddr,
    clang::DiagnosticConsumer*);

namespace Jit
{

ClangCC1Driver::~ClangCC1Driver()
{
  // As long as the driver exists, source files remain on disk to allow
  // debugging JITed code.
  // for (const auto& D : m_deleters)
  //  D();
}

ossia::optional<QDir> ClangCC1Driver::bitcodeDatabase()
{
  auto caches
      = QStandardPaths::standardLocations(QStandardPaths::CacheLocation);
  if (caches.empty())
    caches = QStandardPaths::standardLocations(QStandardPaths::TempLocation);
  if (caches.empty())
    caches
        = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
  if (caches.empty())
    return ossia::none;

  QDir dir{caches.front() + "/score-jit"};
  if (!dir.exists())
    QDir::root().mkpath(dir.absolutePath());

  return dir;
}

static QString hashFile(const QString& path)
{
  QFile f{path};
  SCORE_ASSERT(f.open(QIODevice::ReadOnly));

  QCryptographicHash hash{QCryptographicHash::Sha1};
  hash.addData(&f);

  return hash.result().toBase64(
      QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

llvm::Expected<std::unique_ptr<llvm::Module>>
ClangCC1Driver::compileTranslationUnit(
    const std::string& cpp,
    const std::vector<std::string>& flags,
    CompilerOptions opts,
    llvm::LLVMContext& context)
{
  std::string preproc = replaceExtension(cpp, "preproc.cpp");

  // Default flags
  auto flags_vec = getClangCC1Args(opts);
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

  const auto cache_dir = bitcodeDatabase();

  std::string bitcodeFile;
  auto preproc_hash = hashFile(QString::fromStdString(preproc));
  {
    if (0 && cache_dir && cache_dir->exists())
    {
      QDirIterator it(*cache_dir);
      while (it.hasNext())
      {
        it.next();
        auto fi = it.fileInfo();
        if (fi.fileName() == preproc_hash + ".bc")
        {
          bitcodeFile = fi.absoluteFilePath().toStdString();
          break;
        }
      }
    }
  }

  // If there isn't a matching bitcode file, do the actual C++ -> bitcode
  // compilation
  if (bitcodeFile.empty())
  {
    bitcodeFile = replaceExtension(cpp, "bc");
    flags_vec.resize(flags_vec.size() - 5);
    flags_vec.push_back("-o");
    flags_vec.push_back(bitcodeFile);
    flags_vec.push_back(cpp);

    Timer t;
    llvm::Error err = compileCppToBitcodeFile(flags_vec);
    if (err)
      return std::move(err);

    if (cache_dir && cache_dir->exists())
    {
      QFile f(QString::fromStdString(bitcodeFile));
      if (!f.copy(cache_dir->absolutePath() + "/" + preproc_hash + ".bc"))
      {
        qDebug() << "Writing"
                 << cache_dir->absolutePath() + "/" + preproc_hash + ".bc"
                 << " : failed !";
      }
    }
  }

  // Load the bitcode
  Timer t;
  auto module = readModuleFromBitcodeFile(bitcodeFile, context);

  llvm::sys::fs::remove(bitcodeFile);

  if (!module)
  {
    llvm::sys::fs::remove(cpp);
    return module.takeError();
  }

  m_deleters.push_back([cpp]() { llvm::sys::fs::remove(cpp); });

  return std::move(*module);
}

std::vector<std::string> ClangCC1Driver::getClangCC1Args(CompilerOptions opts)
{
  std::vector<std::string> args;
  args.reserve(200);

  args.push_back("-emit-llvm");
  args.push_back("-emit-llvm-bc");
  args.push_back("-emit-llvm-uselists");

  populateIncludeDirs(args);
  populateCompileOptions(args, opts);
  populateDefinitions(args);

  /*
    for(const auto& arg : args)
    {
      std::cerr << " -- " << arg << std::endl;
    }
    */

  return args;
}

llvm::Error
ClangCC1Driver::compileCppToBitcodeFile(const std::vector<std::string>& args)
{
  std::vector<const char*> argsX;
  argsX.reserve(args.size());
  std::transform(
      args.begin(),
      args.end(),
      std::back_inserter(argsX),
      [](const std::string& s) { return s.c_str(); });

  auto diags = std::make_unique<clang::TextDiagnosticBuffer>();

  if (int res = cc1_main(argsX, "", nullptr, diags.get()))
  {
    std::stringstream ss;
    for (auto it = diags->err_begin(); it != diags->err_end(); ++it)
      ss << "error : " << it->second << "\n";
    return return_code_error(ss.str(), res);
  }

  return llvm::Error::success();
}

}
