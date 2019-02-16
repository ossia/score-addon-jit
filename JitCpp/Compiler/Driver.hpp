#pragma once
#include <JitCpp/Compiler/Compiler.hpp>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Support/PrettyStackTrace.h>

namespace Jit
{

template<typename Fun_T>
struct Driver
{
  Driver(const std::string& fname)
    : X{0, nullptr}
    , jit{*llvm::EngineBuilder().selectTarget()}
    , factory_name{fname}
  {
  }

  std::function<Fun_T> operator()(
        const std::string& sourceCode
        , const std::vector<std::string>& flags)
  {
    auto t0 = std::chrono::high_resolution_clock::now();

    auto sourceFileName = saveSourceFile(sourceCode);
    if (!sourceFileName)
      return {};

    std::string cpp = *sourceFileName;
    auto filename = QFileInfo(QString::fromStdString(cpp)).fileName();
    auto global_init = "_GLOBAL__sub_I_" + filename.replace('-', '_');

    qDebug() << "Looking for: " << global_init;
    auto module = jit.compile(cpp, flags, context);
    {
      auto globals_init = jit.getFunction<void()>(global_init.toStdString());
      if(globals_init)
        (*globals_init)();
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    auto jitedFn = jit.getFunction<Fun_T>(factory_name);
    if (!jitedFn)
      throw Exception{jitedFn.takeError()};

    llvm::outs().flush();
    std::cerr << "\n\nADDON BUILD DURATION: " << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() << " ms \n\n";

    return *jitedFn;
  }

  llvm::PrettyStackTraceProgram X;
  llvm::LLVMContext context;
  JitCompiler jit;
  std::string factory_name;
};

}
