#include "score_addon_jit.hpp"

#include <score/plugins/FactorySetup.hpp>

#include <JitCpp/JitModel.hpp>
#include <JitCpp/ApplicationPlugin.hpp>

#include <Process/Execution/ProcessComponent.hpp>

#include <PluginSettings/PluginSettings.hpp>

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/Signals.h>
score_addon_jit::score_addon_jit()
{
  using namespace llvm;
  sys::PrintStackTraceOnErrorSignal({});

  atexit(llvm_shutdown);
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();
}

score_addon_jit::~score_addon_jit()
{
}

std::vector<std::unique_ptr<score::InterfaceBase>> score_addon_jit::factories(
    const score::ApplicationContext& ctx, const score::InterfaceKey& key) const
{
  return instantiate_factories<
      score::ApplicationContext,
      FW<Process::ProcessModelFactory, Jit::JitEffectFactory>,
      FW<Process::LayerFactory, Jit::LayerFactory>,
      FW<Execution::ProcessComponentFactory,
         Execution::JitEffectComponentFactory>,
      FW<score::SettingsDelegateFactory, PluginSettings::Factory>>(ctx, key);
}

score::GUIApplicationPlugin*
score_addon_jit::make_guiApplicationPlugin(const score::GUIApplicationContext& app)
{
  return new Jit::ApplicationPlugin{app};
}

#include <score/plugins/PluginInstances.hpp>

SCORE_EXPORT_PLUGIN(score_addon_jit)

