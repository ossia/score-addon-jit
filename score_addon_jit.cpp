#include "score_addon_jit.hpp"

#include <PluginSettings/PluginSettings.hpp>
#include <Process/Execution/ProcessComponent.hpp>

#include <score/plugins/FactorySetup.hpp>

#include <JitCpp/ApplicationPlugin.hpp>
#include <JitCpp/JitModel.hpp>
#include <Bytebeat/Bytebeat.hpp>
#include <Texgen/Texgen.hpp>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/TargetSelect.h>
#include <score_addon_jit_commands_files.hpp>
score_addon_jit::score_addon_jit()
{
  using namespace llvm;
  sys::PrintStackTraceOnErrorSignal({});

  atexit(llvm_shutdown);
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();
}

score_addon_jit::~score_addon_jit() {}

std::vector<std::unique_ptr<score::InterfaceBase>> score_addon_jit::factories(
    const score::ApplicationContext& ctx,
    const score::InterfaceKey& key) const
{
  return instantiate_factories<
      score::ApplicationContext,
      FW<Process::ProcessModelFactory
      , Jit::JitEffectFactory
      , Jit::BytebeatEffectFactory
    #if defined(SCORE_JIT_HAS_TEXGEN)
      , Jit::TexgenEffectFactory
    #endif
      >,
      FW<Process::LayerFactory
      , Jit::LayerFactory
      , Jit::BytebeatLayerFactory
    #if defined(SCORE_JIT_HAS_TEXGEN)
      , Jit::TexgenLayerFactory
    #endif
      >,
      FW<Execution::ProcessComponentFactory
      , Execution::JitEffectComponentFactory
      , Jit::BytebeatExecutorFactory
    #if defined(SCORE_JIT_HAS_TEXGEN)
      , Jit::TexgenExecutorFactory
    #endif
      >,
      FW<score::SettingsDelegateFactory
      , PluginSettings::Factory>
      >(ctx, key);
}

std::pair<const CommandGroupKey, CommandGeneratorMap>
score_addon_jit::make_commands()
{
  using namespace Jit;
  std::pair<const CommandGroupKey, CommandGeneratorMap> cmds{
      Jit::CommandFactoryName(), CommandGeneratorMap{}};

  ossia::for_each_type<
#include <score_addon_jit_commands.hpp>
      >(score::commands::FactoryInserter{cmds.second});

  return cmds;
}
score::GUIApplicationPlugin* score_addon_jit::make_guiApplicationPlugin(
    const score::GUIApplicationContext& app)
{
  return new Jit::ApplicationPlugin{app};
}

#include <score/plugins/PluginInstances.hpp>

SCORE_EXPORT_PLUGIN(score_addon_jit)
