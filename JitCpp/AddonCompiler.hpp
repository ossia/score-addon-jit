#pragma once
#include <score/tools/Todo.hpp>

#include <QThread>

#include <score_addon_jit_export.h>
#include <wobjectdefs.h>

namespace score
{
class Plugin_QtInterface;
}

namespace Jit
{
//! Compiles jobs asynchronously
class AddonCompiler final : public QObject
{
  W_OBJECT(AddonCompiler)
public:
  AddonCompiler();

  ~AddonCompiler();
  void submitJob(
      const std::string& id,
      std::string cpp,
      std::vector<std::string> flags) W_SIGNAL(submitJob, id, cpp, flags);
  void jobCompleted(score::Plugin_QtInterface* p) W_SIGNAL(jobCompleted, p);
  void on_job(std::string id, std::string cpp, std::vector<std::string> flags);

private:
  QThread m_thread;
};

using FactoryFunction = std::function<void()>;
using CustomCompiler = std::function<
    FactoryFunction(const std::string&, const std::vector<std::string>&)>;

SCORE_ADDON_JIT_EXPORT
CustomCompiler makeCustomCompiler(const std::string& function);
}

Q_DECLARE_METATYPE(std::string)
W_REGISTER_ARGTYPE(std::string)
Q_DECLARE_METATYPE(std::vector<std::string>)
W_REGISTER_ARGTYPE(std::vector<std::string>)
Q_DECLARE_METATYPE(score::Plugin_QtInterface*)
W_REGISTER_ARGTYPE(score::Plugin_QtInterface*)
