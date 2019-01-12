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

namespace llvm::orc
{

class ScoreLinkingLayerBase {
public:
  using ObjectPtr = std::unique_ptr<MemoryBuffer>;

protected:

  /// Holds an object to be allocated/linked as a unit in the JIT.
  ///
  /// An instance of this class will be created for each object added
  /// via JITObjectLayer::addObject. Deleting the instance (via
  /// removeObject) frees its memory, removing all symbol definitions that
  /// had been provided by this instance. Higher level layers are responsible
  /// for taking any action required to handle the missing symbols.
  class LinkedObject {
  public:
    LinkedObject() = default;
    LinkedObject(const LinkedObject&) = delete;
    void operator=(const LinkedObject&) = delete;
    virtual ~LinkedObject() = default;

    virtual Error finalize() = 0;

    virtual JITSymbol::GetAddressFtor
    getSymbolMaterializer(std::string Name) = 0;

    virtual void mapSectionAddress(const void *LocalAddress,
                                   JITTargetAddress TargetAddr) const = 0;

    JITSymbol getSymbol(StringRef Name, bool ExportedSymbolsOnly) {
      auto SymEntry = SymbolTable.find(Name);
      if (SymEntry == SymbolTable.end())
        return nullptr;
      if (!SymEntry->second.getFlags().isExported() && ExportedSymbolsOnly)
        return nullptr;
      if (!Finalized)
        return JITSymbol(getSymbolMaterializer(Name),
                         SymEntry->second.getFlags());
      return JITSymbol(SymEntry->second);
    }

  protected:
    StringMap<JITEvaluatedSymbol> SymbolTable;
    bool Finalized = false;
  };
};

class ScoreLinkingLayer : public ScoreLinkingLayerBase {
public:

  using ScoreLinkingLayerBase::ObjectPtr;

  /// Functor for receiving object-loaded notifications.
  using NotifyLoadedFtor =
      std::function<void(VModuleKey, const object::ObjectFile &Obj,
                         const RuntimeDyld::LoadedObjectInfo &)>;

  /// Functor for receiving finalization notifications.
  using NotifyFinalizedFtor =
      std::function<void(VModuleKey, const object::ObjectFile &Obj,
                         const RuntimeDyld::LoadedObjectInfo &)>;

  /// Functor for receiving deallocation notifications.
  using NotifyFreedFtor = std::function<void(VModuleKey, const object::ObjectFile &Obj)>;

private:
  using OwnedObject = object::OwningBinary<object::ObjectFile>;

  template <typename MemoryManagerPtrT>
  class ConcreteLinkedObject : public LinkedObject {
  public:
    ConcreteLinkedObject(ScoreLinkingLayer &Parent, VModuleKey K,
                         OwnedObject Obj, MemoryManagerPtrT MemMgr,
                         std::shared_ptr<SymbolResolver> Resolver,
                         bool ProcessAllSections)
        : K(std::move(K)),
          Parent(Parent),
          MemMgr(std::move(MemMgr)),
          PFC(llvm::make_unique<PreFinalizeContents>(
              std::move(Obj), std::move(Resolver),
              ProcessAllSections)) {
      buildInitialSymbolTable(PFC->Obj);
    }

    ~ConcreteLinkedObject() override {
      if (this->Parent.NotifyFreed)
        this->Parent.NotifyFreed(K, *ObjForNotify.getBinary());

      MemMgr->deregisterEHFrames();
    }

    Error finalize() override {
      assert(PFC && "mapSectionAddress called on finalized LinkedObject");

      JITSymbolResolverAdapter ResolverAdapter(Parent.ES, *PFC->Resolver,
                 nullptr);
      PFC->RTDyld = llvm::make_unique<RuntimeDyld>(*MemMgr, ResolverAdapter);
      PFC->RTDyld->setProcessAllSections(PFC->ProcessAllSections);

      Finalized = true;

      std::unique_ptr<RuntimeDyld::LoadedObjectInfo> Info =
          PFC->RTDyld->loadObject(*PFC->Obj.getBinary());

      // Copy the symbol table out of the RuntimeDyld instance.
      {
        auto SymTab = PFC->RTDyld->getSymbolTable();
        for (auto &KV : SymTab)
          SymbolTable[KV.first] = KV.second;
      }

      if (Parent.NotifyLoaded)
        Parent.NotifyLoaded(K, *PFC->Obj.getBinary(), *Info);

      PFC->RTDyld->finalizeWithMemoryManagerLocking();

      if (PFC->RTDyld->hasError())
        return make_error<StringError>(PFC->RTDyld->getErrorString(),
                                       inconvertibleErrorCode());

      if (Parent.NotifyFinalized)
        Parent.NotifyFinalized(K, *PFC->Obj.getBinary(), *Info);

      // Release resources.
      if (this->Parent.NotifyFreed)
        ObjForNotify = std::move(PFC->Obj); // needed for callback
      PFC = nullptr;
      return Error::success();
    }

    JITSymbol::GetAddressFtor getSymbolMaterializer(std::string Name) override {
      return [this, Name]() -> Expected<JITTargetAddress> {
        // The symbol may be materialized between the creation of this lambda
        // and its execution, so we need to double check.
        if (!this->Finalized)
          if (auto Err = this->finalize())
            return std::move(Err);
        return this->getSymbol(Name, false).getAddress();
      };
    }

    void mapSectionAddress(const void *LocalAddress,
                           JITTargetAddress TargetAddr) const override {
      assert(PFC && "mapSectionAddress called on finalized LinkedObject");
      assert(PFC->RTDyld && "mapSectionAddress called on raw LinkedObject");
      PFC->RTDyld->mapSectionAddress(LocalAddress, TargetAddr);
    }

  private:
    void buildInitialSymbolTable(const OwnedObject &Obj) {
      for (auto &Symbol : Obj.getBinary()->symbols()) {
        if (Symbol.getFlags() & object::SymbolRef::SF_Undefined)
          continue;
        Expected<StringRef> SymbolName = Symbol.getName();
        // FIXME: Raise an error for bad symbols.
        if (!SymbolName) {
          consumeError(SymbolName.takeError());
          continue;
        }
#if (LLVM_VERSION_MAJOR < 8)
        auto Flags = JITSymbolFlags::fromObjectSymbol(Symbol);
#else
        auto Flags = *JITSymbolFlags::fromObjectSymbol(Symbol);
#endif
        SymbolTable.insert(
          std::make_pair(*SymbolName, JITEvaluatedSymbol(0, Flags)));
      }
    }

    // Contains the information needed prior to finalization: the object files,
    // memory manager, resolver, and flags needed for RuntimeDyld.
    struct PreFinalizeContents {
      PreFinalizeContents(OwnedObject Obj,
                          std::shared_ptr<SymbolResolver> Resolver,
                          bool ProcessAllSections)
          : Obj(std::move(Obj)),
            Resolver(std::move(Resolver)),
            ProcessAllSections(ProcessAllSections) {}

      OwnedObject Obj;
      std::shared_ptr<SymbolResolver> Resolver;
      bool ProcessAllSections;
      std::unique_ptr<RuntimeDyld> RTDyld;
    };

    VModuleKey K;
    ScoreLinkingLayer &Parent;
    MemoryManagerPtrT MemMgr;
    OwnedObject ObjForNotify;
    std::unique_ptr<PreFinalizeContents> PFC;
  };

  template <typename MemoryManagerPtrT>
  std::unique_ptr<ConcreteLinkedObject<MemoryManagerPtrT>>
  createLinkedObject(ScoreLinkingLayer &Parent, VModuleKey K,
                     OwnedObject Obj, MemoryManagerPtrT MemMgr,
                     std::shared_ptr<SymbolResolver> Resolver,
                     bool ProcessAllSections) {
    using LOS = ConcreteLinkedObject<MemoryManagerPtrT>;
    return llvm::make_unique<LOS>(Parent, std::move(K), std::move(Obj),
                                  std::move(MemMgr), std::move(Resolver),
                                  ProcessAllSections);
  }

public:
  struct Resources {
    std::shared_ptr<RuntimeDyld::MemoryManager> MemMgr;
    std::shared_ptr<SymbolResolver> Resolver;
  };

  using ResourcesGetter = std::function<Resources(VModuleKey)>;

  /// Construct an ObjectLinkingLayer with the given NotifyLoaded,
  ///        and NotifyFinalized functors.
  ScoreLinkingLayer(
      ExecutionSession &ES, ResourcesGetter GetResources,
      NotifyLoadedFtor NotifyLoaded = NotifyLoadedFtor(),
      NotifyFinalizedFtor NotifyFinalized = NotifyFinalizedFtor(),
      NotifyFreedFtor NotifyFreed = NotifyFreedFtor())
      : ES(ES), GetResources(std::move(GetResources)),
        NotifyLoaded(std::move(NotifyLoaded)),
        NotifyFinalized(std::move(NotifyFinalized)),
        NotifyFreed(std::move(NotifyFreed)),
        ProcessAllSections(false) {
  }

  /// Set the 'ProcessAllSections' flag.
  ///
  /// If set to true, all sections in each object file will be allocated using
  /// the memory manager, rather than just the sections required for execution.
  ///
  /// This is kludgy, and may be removed in the future.
  void setProcessAllSections(bool ProcessAllSections) {
    this->ProcessAllSections = ProcessAllSections;
  }

  /// Add an object to the JIT.
  Error addObject(VModuleKey K, ObjectPtr ObjBuffer) {

    auto Obj =
        object::ObjectFile::createObjectFile(ObjBuffer->getMemBufferRef());
    if (!Obj)
      return Obj.takeError();

    assert(!LinkedObjects.count(K) && "VModuleKey already in use");

    auto R = GetResources(K);

    LinkedObjects[K] = createLinkedObject(
        *this, K, OwnedObject(std::move(*Obj), std::move(ObjBuffer)),
        std::move(R.MemMgr), std::move(R.Resolver), ProcessAllSections);

    return Error::success();
  }

  /// Remove the object associated with VModuleKey K.
  ///
  ///   All memory allocated for the object will be freed, and the sections and
  /// symbols it provided will no longer be available. No attempt is made to
  /// re-emit the missing symbols, and any use of these symbols (directly or
  /// indirectly) will result in undefined behavior. If dependence tracking is
  /// required to detect or resolve such issues it should be added at a higher
  /// layer.
  Error removeObject(VModuleKey K) {
    assert(LinkedObjects.count(K) && "VModuleKey not associated with object");
    // How do we invalidate the symbols in H?
    LinkedObjects.erase(K);
    return Error::success();
  }

  /// Search for the given named symbol.
  /// @param Name The name of the symbol to search for.
  /// @param ExportedSymbolsOnly If true, search only for exported symbols.
  /// @return A handle for the given named symbol, if it exists.
  JITSymbol findSymbol(StringRef Name, bool ExportedSymbolsOnly) {
    for (auto &KV : LinkedObjects)
      if (auto Sym = KV.second->getSymbol(Name, ExportedSymbolsOnly))
        return Sym;
      else if (auto Err = Sym.takeError())
        return std::move(Err);

    return nullptr;
  }

  /// Search for the given named symbol in the context of the loaded
  ///        object represented by the VModuleKey K.
  /// @param K The VModuleKey for the object to search in.
  /// @param Name The name of the symbol to search for.
  /// @param ExportedSymbolsOnly If true, search only for exported symbols.
  /// @return A handle for the given named symbol, if it is found in the
  ///         given object.
  JITSymbol findSymbolIn(VModuleKey K, StringRef Name,
                         bool ExportedSymbolsOnly) {
    assert(LinkedObjects.count(K) && "VModuleKey not associated with object");
    return LinkedObjects[K]->getSymbol(Name, ExportedSymbolsOnly);
  }

  /// Map section addresses for the object associated with the
  ///        VModuleKey K.
  void mapSectionAddress(VModuleKey K, const void *LocalAddress,
                         JITTargetAddress TargetAddr) {
    assert(LinkedObjects.count(K) && "VModuleKey not associated with object");
    LinkedObjects[K]->mapSectionAddress(LocalAddress, TargetAddr);
  }

  /// Immediately emit and finalize the object represented by the given
  ///        VModuleKey.
  /// @param K VModuleKey for object to emit/finalize.
  Error emitAndFinalize(VModuleKey K) {
    assert(LinkedObjects.count(K) && "VModuleKey not associated with object");
    return LinkedObjects[K]->finalize();
  }

private:
  ExecutionSession &ES;

  std::map<VModuleKey, std::unique_ptr<LinkedObject>> LinkedObjects;
  ResourcesGetter GetResources;
  NotifyLoadedFtor NotifyLoaded;
  NotifyFinalizedFtor NotifyFinalized;
  NotifyFreedFtor NotifyFreed;
  bool ProcessAllSections = false;
};

}
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
#if (LLVM_VERSION_MAJOR < 8)
      // Workaround 5.0 API inconsistency:
      // http://lists.llvm.org/pipermail/llvm-dev/2017-August/116806.html
      const auto& fixedInfo
          = static_cast<const llvm::RuntimeDyld::LoadedObjectInfo&>(info);

      Jit.GdbEventListener->NotifyObjectEmitted(obj, fixedInfo);
#endif
    }

  private:
    JitCompiler& Jit;
  };

  using ModulePtr_t = std::unique_ptr<llvm::Module>;
  using IRCompiler_t = llvm::orc::SimpleCompiler;

  using ObjectLayer_t = llvm::orc::ScoreLinkingLayer;
  using CompileLayer_t
#if (LLVM_VERSION_MAJOR < 8) || defined(_WIN32)
      = llvm::orc::IRCompileLayer<ObjectLayer_t, IRCompiler_t>;
#else
      = llvm::orc::LegacyIRCompileLayer<ObjectLayer_t, IRCompiler_t>;
#endif
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

  auto compile(
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
