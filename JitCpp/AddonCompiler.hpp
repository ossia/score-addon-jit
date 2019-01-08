#pragma once
#include <score/tools/Todo.hpp>
#include <llvm/IR/Module.h>
#include <wobjectdefs.h>
#include <QThread>

namespace score
{ class Plugin_QtInterface; }

namespace Jit
{
struct jit_plugin
{
  //std::unique_ptr<llvm::Module> module;
  score::Plugin_QtInterface* plugin{};
};

//! Compiles jobs asynchronously
class AddonCompiler final
    : public QObject
{
  W_OBJECT(AddonCompiler)
public:
  AddonCompiler();

  ~AddonCompiler();
  void submitJob(const std::string& id, std::string cpp, std::vector<std::string> flags)
  W_SIGNAL(submitJob, id, cpp, flags);
  void jobCompleted(jit_plugin* p)
  W_SIGNAL(jobCompleted, p);
  void on_job(const std::string& id, const std::string& cpp, const std::vector<std::string>& flags);

private:
  QThread m_thread;
};

}

Q_DECLARE_METATYPE(std::string)
W_REGISTER_ARGTYPE(std::string)
Q_DECLARE_METATYPE(std::vector<std::string>)
W_REGISTER_ARGTYPE(std::vector<std::string>)
Q_DECLARE_METATYPE(Jit::jit_plugin*)
W_REGISTER_ARGTYPE(Jit::jit_plugin*)
