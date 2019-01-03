#pragma once
#include <JitCpp/AddonCompiler.hpp>

#include <score/plugins/application/GUIApplicationPlugin.hpp>

#include <QFileSystemWatcher>
#include <QThread>

namespace Jit
{

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
