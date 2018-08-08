#include "score_addon_jit.hpp"
#include <score/plugins/customfactory/FactorySetup.hpp>
#include <JitCpp/JitModel.hpp>

score_addon_jit::score_addon_jit()
{
}

score_addon_jit::~score_addon_jit()
{

}

std::vector<std::unique_ptr<score::InterfaceBase> >
score_addon_jit::factories(
    const score::ApplicationContext& ctx,
    const score::InterfaceKey& key) const
{
  return instantiate_factories<
          score::ApplicationContext,
      FW<Process::ProcessModelFactory
          , Jit::JitEffectFactory
          >,
      FW<Process::LayerFactory
          , Jit::LayerFactory
          >,
      FW<Execution::ProcessComponentFactory
          , Execution::JitEffectComponentFactory
          >
  >(ctx, key);
}

#include <score/plugins/PluginInstances.hpp>
SCORE_EXPORT_PLUGIN(score_addon_jit)
