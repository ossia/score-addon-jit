#include <JitCpp/AddonCompiler.hpp>
#include <JitCpp/Jit.hpp>
#include <wobjectimpl.h>

W_OBJECT_IMPL(Jit::AddonCompiler)

namespace Jit
{

struct jit_plugin_context
{
  jit_plugin_context()
    : X{0, nullptr}
    , jit{*llvm::EngineBuilder().selectTarget()}
  {
  }

  jit_plugin compile(const std::string& id, const std::string& sourceCode, std::vector<std::string> flags)
  {
    auto t0 = std::chrono::high_resolution_clock::now();

    flags.push_back("-DSCORE_JIT_ID=" + id);

    auto sourceFileName = saveSourceFile(sourceCode);
    if (!sourceFileName)
      return {};

    std::string cpp = *sourceFileName;
    auto filename = QFileInfo(QString::fromStdString(cpp)).fileName();
    auto global_init = "_GLOBAL__sub_I_" + filename.replace('-', '_');

    auto module = jit.compile(cpp, flags, context);
    {
      auto globals_init = jit.getFunction<void()>(global_init.toStdString());
      SCORE_ASSERT(globals_init);
      (*globals_init)();
    }

    auto jitedFn = jit.getFunction<score::Plugin_QtInterface* ()>("plugin_instance_" + id);
    if (!jitedFn)
      throw Exception{jitedFn.takeError()};

    auto instance = (*jitedFn)();
    if (!jitedFn)
      throw std::runtime_error("No instance of plug-in");

    llvm::outs().flush();
    auto t1 = std::chrono::high_resolution_clock::now();
    std::cerr << "\n\nADDON BUILD DURATION: " << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() << " ms \n\n";

    return {instance};
  }

  llvm::PrettyStackTraceProgram X;
  llvm::LLVMContext context;
  JitCompiler jit;
};

AddonCompiler::AddonCompiler()
{
  connect(this, &AddonCompiler::submitJob, this, &AddonCompiler::on_job, Qt::QueuedConnection);
  this->moveToThread(&m_thread);
  m_thread.start();
}

AddonCompiler::~AddonCompiler()
{
  m_thread.exit(0);
  m_thread.wait();
}

void AddonCompiler::on_job(const std::string& id, const std::string& cpp, const std::vector<std::string>& flags)
{
  try
  {
    // TODO this is needed because if the jit_plugin instance is removed,
    // function calls to this plug-in will crash. We must detect when a plugin is
    // not necessary anymore and remove it.

    static std::list<std::unique_ptr<jit_plugin_context>> ctx;
    static std::list<jit_plugin> plugs;

    ctx.push_back(std::make_unique<jit_plugin_context>());
    auto plug = ctx.back()->compile(id, cpp, flags);
    if(plug.plugin)
    {
      plugs.push_front(std::move(plug));
      jobCompleted(&plugs.front());
    }
  }
  catch(const std::runtime_error& e) {
    qDebug() << "could not compile plug-in: " << e.what();
  }
}

}
