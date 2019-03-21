#include <JitCpp/AddonCompiler.hpp>
#include <JitCpp/Compiler/Driver.hpp>
#include <wobjectimpl.h>

W_OBJECT_IMPL(Jit::AddonCompiler)

namespace Jit
{
template <typename Fun_T>
struct CompilerWrapper
{
  using impl_t = Driver<Fun_T>;
  impl_t* m_impl{};

  CompilerWrapper() = default;
  ~CompilerWrapper() { delete m_impl; }

  CompilerWrapper(const std::string& s) : m_impl{new impl_t{s}} {}

  CompilerWrapper(const CompilerWrapper& other)
  {
    if (other.m_impl)
      m_impl = new impl_t{other.m_impl->factory_name};
  }

  CompilerWrapper& operator=(const CompilerWrapper& other)
  {
    delete m_impl;
    if (other.m_impl)
      m_impl = {new impl_t{other.m_impl->factory_name}};
    else
      m_impl = nullptr;
    return *this;
  }

  std::function<Fun_T>
  operator()(const std::string& code, const std::vector<std::string>& args)
  {
    if (m_impl)
      return (*m_impl)(code, args);
    return {};
  }
};

AddonCompiler::AddonCompiler()
{
  connect(
      this,
      &AddonCompiler::submitJob,
      this,
      &AddonCompiler::on_job,
      Qt::QueuedConnection);
  //this->moveToThread(&m_thread);
  //m_thread.start();
}

AddonCompiler::~AddonCompiler()
{
  //m_thread.exit(0);
  //m_thread.wait();
}

void AddonCompiler::on_job(
    std::string id,
    std::string cpp,
    std::vector<std::string> flags)
{
  try
  {
    // TODO this is needed because if the jit_plugin instance is removed,
    // function calls to this plug-in will crash. We must detect when a plugin
    // is not necessary anymore and remove it.
    using compiler_t = Driver<score::Plugin_QtInterface*()>;

    static std::list<std::unique_ptr<compiler_t>> ctx;

    flags.push_back("-DSCORE_JIT_ID=" + id);
    ctx.push_back(std::make_unique<compiler_t>("plugin_instance_" + id));
    auto jitedFn = (*ctx.back())(cpp, flags);

    auto instance = jitedFn();
    if (!instance)
      throw std::runtime_error("No instance of plug-in");

    jobCompleted(instance);
  }
  catch (const std::runtime_error& e)
  {
    qDebug() << "could not compile plug-in: " << e.what();
  }
}

CustomCompiler makeCustomCompiler(const std::string& function)
{
  return CompilerWrapper<void()>{function};
}

}
