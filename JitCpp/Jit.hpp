#pragma once

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//
// Based on the code in
// https://github.com/weliveindetail/JitFromScratch
//
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/JITEventListener.h>
#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/ExecutionEngine/Orc/IRCompileLayer.h>
#include <llvm/ExecutionEngine/Orc/LambdaResolver.h>
#include <llvm/ExecutionEngine/Orc/OrcError.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/RuntimeDyld.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Mangler.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>

#include <JitCpp/JitPlatform.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
extern int cc1_main(llvm::ArrayRef<const char*> Argv, const char* Argv0, void* MainAddr);
namespace Jit
{

inline llvm::Expected<std::unique_ptr<llvm::Module>>
readModuleFromBitcodeFile(llvm::StringRef bc, llvm::LLVMContext& context)
{
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> buffer
      = llvm::MemoryBuffer::getFile(bc);
  if (!buffer)
    return llvm::errorCodeToError(buffer.getError());

  return llvm::parseBitcodeFile(buffer.get()->getMemBufferRef(), context);
}


class ClangCC1Driver
{
public:
  ClangCC1Driver() = default;

  // As long as the driver exists, source files remain on disk to allow
  // debugging JITed code.
  ~ClangCC1Driver()
  {
    for (const auto& D : SourceFileDeleters)
      D();
  }
  static std::vector<std::string>
  getClangCC1Args(llvm::StringRef cpp, llvm::StringRef bc)
  {
    std::vector<std::string> args;

    args.push_back("-emit-llvm");
    args.push_back("-emit-llvm-bc");
    args.push_back("-emit-llvm-uselists");

    args.push_back("-main-file-name");
    args.push_back(cpp.data());

    populateIncludeDirs(args);
    populateCompileOptions(args);
    populateDefinitions(args);

    args.push_back("-o");
    args.push_back(bc.data());
    args.push_back("-x");
    args.push_back("c++");
    args.push_back(cpp.data());

    for(const auto& arg : args)
    {
      std::cerr << " -- " << arg << std::endl;
    }

    return args;
  }

  llvm::Expected<std::unique_ptr<llvm::Module>>
  compileTranslationUnit(const std::string& cppCode, const std::vector<std::string>& flags, llvm::LLVMContext& context)
  {
    auto sourceFileName = saveSourceFile(cppCode);
    if (!sourceFileName)
      return sourceFileName.takeError();

    std::string cpp = *sourceFileName;
    std::string bc = replaceExtension(cpp, "bc");

    auto flags_vec = getClangCC1Args(cpp, bc);
    flags_vec.insert(flags_vec.end(), flags.begin(), flags.end());
    llvm::Error err = compileCppToBitcodeFile(flags_vec);
    if (err)
      return std::move(err);

    auto module = readModuleFromBitcodeFile(bc, context);

    llvm::sys::fs::remove(bc);

    if (!module)
    {
      llvm::sys::fs::remove(cpp);
      return module.takeError();
    }

    SourceFileDeleters.push_back([cpp]() { llvm::sys::fs::remove(cpp); });

    return std::move(*module);
  }

  static llvm::Error return_code_error(llvm::StringRef message, int returnCode)
  {
    return llvm::make_error<llvm::StringError>(
        message, std::error_code(returnCode, std::system_category()));
  }

  static llvm::Expected<std::string> saveSourceFile(const std::string& content)
  {
    using llvm::sys::fs::createTemporaryFile;

    int fd;
    llvm::SmallString<128> name;
    if (auto ec = createTemporaryFile("score-addon-cpp", "cpp", fd, name))
      return llvm::errorCodeToError(ec);

    constexpr bool shouldClose = true;
    constexpr bool unbuffered = true;
    llvm::raw_fd_ostream os(fd, shouldClose, unbuffered);
    os << content;

    return name.str();
  }

  static std::string
  replaceExtension(llvm::StringRef name, llvm::StringRef ext)
  {
    return name.substr(0, name.find_last_of('.') + 1).str() + ext.str();
  }

  static llvm::Error compileCppToBitcodeFile(std::vector<std::string> args)
  {
    std::vector<const char*> argsX;
    std::transform(
        args.begin(), args.end(), std::back_inserter(argsX),
        [](const std::string& s) { return s.c_str(); });

    if (int res = cc1_main(argsX, "", nullptr))
      return return_code_error("Clang cc1 compilation failed", res);

    return llvm::Error::success();
  }

private:
  std::vector<std::function<void()>> SourceFileDeleters;
};

class SimpleOrcJit
{
  struct NotifyObjectLoaded_t
  {
    NotifyObjectLoaded_t(SimpleOrcJit& jit) : Jit(jit)
    {
    }

    // Called by the ObjectLayer for each emitted object.
    // Forward notification to GDB JIT interface.
    void operator()(llvm::orc::VModuleKey,
        const llvm::object::ObjectFile& obj,
        const llvm::LoadedObjectInfo& info)
    {
      // Workaround 5.0 API inconsistency:
      // http://lists.llvm.org/pipermail/llvm-dev/2017-August/116806.html
      const auto& fixedInfo
          = static_cast<const llvm::RuntimeDyld::LoadedObjectInfo&>(info);

      Jit.GdbEventListener->NotifyObjectEmitted(obj, fixedInfo);
    }

  private:
    SimpleOrcJit& Jit;
  };

  using ModulePtr_t = std::unique_ptr<llvm::Module>;
  using IRCompiler_t = llvm::orc::SimpleCompiler;

  using ObjectLayer_t = llvm::orc::RTDyldObjectLinkingLayer;
  using CompileLayer_t
      = llvm::orc::IRCompileLayer<ObjectLayer_t, IRCompiler_t>;

  llvm::orc::ExecutionSession es;
public:
  std::shared_ptr<llvm::RuntimeDyld::MemoryManager> m_memoryManager = std::make_shared<llvm::SectionMemoryManager>();
  SimpleOrcJit(llvm::TargetMachine& targetMachine)
      : DL(targetMachine.createDataLayout())
      , SymbolResolverPtr{llvm::orc::createLegacyLookupResolver(es, [&] (const std::string& name) {
    if(auto res = findSymbolInJITedCode(name))
      return res;
    return findSymbolInHostProcess(name);
  }, {})}
      , NotifyObjectLoaded(*this)
      , ObjectLayer(es, [this] (llvm::orc::VModuleKey) { return ObjectLayer_t::Resources{m_memoryManager, SymbolResolverPtr}; }, NotifyObjectLoaded)
      , CompileLayer(ObjectLayer, IRCompiler_t(targetMachine))
  {
    // Load own executable as dynamic library.
    // Required for RTDyldMemoryManager::getSymbolAddressInProcess().
    llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);

    // Internally points to a llvm::ManagedStatic.
    // No need to free. "create" is a misleading term here.
    GdbEventListener = llvm::JITEventListener::createGDBRegistrationListener();
  }

  void submitModule(ModulePtr_t module)
  {
    // Commit module for compilation to machine code. Actual compilation
    // happens on demand as soon as one of it's symbols is accessed. None of
    // the layers used here issue Errors from this call.
    static llvm::orc::VModuleKey k = 0;
    k++;
    llvm::cantFail(
        CompileLayer.addModule(k, std::move(module)));
  }

  llvm::Expected<std::unique_ptr<llvm::Module>>
  compileModuleFromCpp(const std::string& cppCode, const std::vector<std::string>& flags, llvm::LLVMContext& context)
  {
    return ClangDriver.compileTranslationUnit(cppCode, flags, context);
  }

  template <class Signature_t>
  llvm::Expected<std::function<Signature_t>> getFunction(std::string name)
  {
    using namespace llvm;

    // Find symbol name in committed modules.
    std::string mangledName = mangle(std::move(name));
    JITSymbol sym = findSymbolInJITedCode(mangledName);
    if (!sym)
      return make_error<orc::JITSymbolNotFound>(mangledName);

    // Access symbol address.
    // Invokes compilation for the respective module if not compiled yet.
    Expected<JITTargetAddress> addr = sym.getAddress();
    if (!addr)
      return addr.takeError();

    auto typedFunctionPtr = reinterpret_cast<Signature_t*>(*addr);
    return std::function<Signature_t>(typedFunctionPtr);
  }

private:
  llvm::DataLayout DL;
  ClangCC1Driver ClangDriver;
  std::shared_ptr<llvm::orc::SymbolResolver> SymbolResolverPtr;
  NotifyObjectLoaded_t NotifyObjectLoaded;
  llvm::JITEventListener* GdbEventListener;

  ObjectLayer_t ObjectLayer;
  CompileLayer_t CompileLayer;

  llvm::JITSymbol findSymbolInJITedCode(std::string mangledName)
  {
    constexpr bool exportedSymbolsOnly = false;
    return CompileLayer.findSymbol(mangledName, exportedSymbolsOnly);
  }

  llvm::JITSymbol findSymbolInHostProcess(std::string mangledName)
  {
    // Lookup function address in the host symbol table.
    if (llvm::JITTargetAddress addr
        = llvm::RTDyldMemoryManager::getSymbolAddressInProcess(mangledName))
      return llvm::JITSymbol(addr, llvm::JITSymbolFlags::Exported);

    return nullptr;
  }

  // System name mangler: may prepend '_' on OSX or '\x1' on Windows
  std::string mangle(std::string name)
  {
    std::string buffer;
    llvm::raw_string_ostream ostream(buffer);
    llvm::Mangler::getNameWithPrefix(ostream, std::move(name), DL);
    return ostream.str();
  }
};

struct jit_error : std::runtime_error
{
  using std::runtime_error::runtime_error;
  jit_error(llvm::Error E) : std::runtime_error{"JIT error"}
  {
    llvm::handleAllErrors(std::move(E), [&](const llvm::ErrorInfoBase& EI) {
      llvm::errs() << "Fatal Error: ";
      EI.log(llvm::errs());
      llvm::errs() << "\n";
      llvm::errs().flush();
    });
  }
};



struct jit_ctx;
struct jit_object
{
  std::unique_ptr<llvm::Module> module;
  std::function<void*()> factory;
  std::shared_ptr<jit_ctx> context;
};
struct jit_ctx : public std::enable_shared_from_this<jit_ctx>
{
  static inline struct init
  {
    init()
    {
      using namespace llvm;

      sys::PrintStackTraceOnErrorSignal({});

      atexit(llvm_shutdown);
      InitializeNativeTarget();
      InitializeNativeTargetAsmPrinter();
      InitializeNativeTargetAsmParser();
    }
  } _init;
  jit_ctx() : X{0, nullptr}, jit{*llvm::EngineBuilder().selectTarget()}
  {
  }

  jit_object compile(
      const std::string& sourceCode
      , const std::string& symbol
      , const std::vector<std::string>& additional_flags = {})
  {
    auto t0 = std::chrono::high_resolution_clock::now();
    auto module = jit.compileModuleFromCpp(sourceCode, additional_flags, context);
    if (!module)
      throw jit_error{module.takeError()};

    // Compile to machine code and link.
    jit.submitModule(std::move(*module));
    auto f = jit.getFunction<void*()>(symbol);
    if (!f)
      throw jit_error{f.takeError()};

    auto t1 = std::chrono::high_resolution_clock::now();
    std::cerr << "\n\nBUILD DURATION: " << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() << " ms \n\n";
    llvm::outs().flush();
    return {std::move(*module), *f, shared_from_this()};
  }

  llvm::PrettyStackTraceProgram X;
  llvm::LLVMContext context;
  SimpleOrcJit jit;
};



}
