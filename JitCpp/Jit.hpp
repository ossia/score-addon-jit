#pragma once

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//
// Based on the code in
// https://github.com/weliveindetail/JitFromScratch
//
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/StringRef.h>
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
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>

#include <JitCpp/ClangDriver.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <sstream>

namespace Jit
{

class JitCompiler
{
  struct NotifyObjectLoaded_t
  {
    NotifyObjectLoaded_t(JitCompiler& jit) : Jit(jit)
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
    JitCompiler& Jit;
  };

  using ModulePtr_t = std::unique_ptr<llvm::Module>;
  using IRCompiler_t = llvm::orc::SimpleCompiler;

  using ObjectLayer_t = llvm::orc::RTDyldObjectLinkingLayer;
  using CompileLayer_t
      = llvm::orc::IRCompileLayer<ObjectLayer_t, IRCompiler_t>;

  llvm::orc::ExecutionSession es;
public:
  std::shared_ptr<llvm::RuntimeDyld::MemoryManager> m_memoryManager = std::make_shared<llvm::SectionMemoryManager>();
  JitCompiler(llvm::TargetMachine& targetMachine)
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

  std::unique_ptr<llvm::Module> compile(
      const std::string& cppCode
      , const std::vector<std::string>& flags
      , llvm::LLVMContext& context)
  {
    auto module = compileModule(cppCode, flags, context);
    if (!module)
      throw Exception{module.takeError()};

    // Compile to machine code and link.
    submitModule(std::move(*module));

    return std::move(*module);
  }

  llvm::Expected<std::unique_ptr<llvm::Module>>
  compileModule(
      const std::string& cppCode
      , const std::vector<std::string>& flags
      , llvm::LLVMContext& context)
  {
    return ClangDriver.compileTranslationUnit(cppCode, flags, context);
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
  llvm::JITSymbol findSymbolInJITedCode(std::string mangledName)
  {
    constexpr bool exportedSymbolsOnly = false;
    return CompileLayer.findSymbol(mangledName, exportedSymbolsOnly);
  }

  llvm::JITSymbol findSymbolInHostProcess(std::string mangledName) const
  {
    // Lookup function address in the host symbol table.
    if (llvm::JITTargetAddress addr
        = llvm::RTDyldMemoryManager::getSymbolAddressInProcess(mangledName))
      return llvm::JITSymbol(addr, llvm::JITSymbolFlags::Exported);

    return nullptr;
  }

  // System name mangler: may prepend '_' on OSX or '\x1' on Windows
  std::string mangle(std::string name) const noexcept
  {
    std::string buffer;
    llvm::raw_string_ostream ostream(buffer);
    llvm::Mangler::getNameWithPrefix(ostream, std::move(name), DL);
    return ostream.str();
  }

  llvm::DataLayout DL;
  ClangCC1Driver ClangDriver;
  std::shared_ptr<llvm::orc::SymbolResolver> SymbolResolverPtr;
  NotifyObjectLoaded_t NotifyObjectLoaded;
  llvm::JITEventListener* GdbEventListener;

  ObjectLayer_t ObjectLayer;
  CompileLayer_t CompileLayer;
};
}
