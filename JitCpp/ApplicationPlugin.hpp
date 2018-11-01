#pragma once
#include <score/plugins/application/GUIApplicationPlugin.hpp>
#include <QFileSystemWatcher>
#include <QThread>
namespace llvm
{
class Module;
}

namespace Jit
{
struct jit_plugin
{
  std::unique_ptr<llvm::Module> module;
  score::Plugin_QtInterface* plugin{};
};
struct AddonCompiler : QObject
{
  W_OBJECT(AddonCompiler)
public:
  AddonCompiler();

  ~AddonCompiler();
  void submitJob(std::string cpp, std::vector<std::string> flags)
  W_SIGNAL(submitJob, cpp, flags);
  void jobCompleted(jit_plugin* p)
  W_SIGNAL(jobCompleted, p);
  void on_job(const std::string& cpp, const std::vector<std::string> & flags);

private:
  QThread m_thread;
};

struct ApplicationPlugin final
    : public QObject
    , public score::GUIApplicationPlugin
{
  ApplicationPlugin(const score::GUIApplicationContext& ctx);

  void setupAddon(const QString& addon);
  void registerAddon(jit_plugin*);
  void updateAddon(const QString& addon);

  void setupNode(const QString& addon);
  void initialize() override;

  QFileSystemWatcher m_addonsWatch;
  QFileSystemWatcher m_nodesWatch;
  AddonCompiler m_compiler;
};
}

Q_DECLARE_METATYPE(std::string)
W_REGISTER_ARGTYPE(std::string)
Q_DECLARE_METATYPE(std::vector<std::string>)
W_REGISTER_ARGTYPE(std::vector<std::string>)
Q_DECLARE_METATYPE(Jit::jit_plugin*)
W_REGISTER_ARGTYPE(Jit::jit_plugin*)
