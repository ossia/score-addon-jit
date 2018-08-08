#pragma once
#include <score/plugins/qt_interfaces/PluginRequirements_QtInterface.hpp>
#include <score/plugins/qt_interfaces/FactoryInterface_QtInterface.hpp>
#include <score/plugins/qt_interfaces/CommandFactory_QtInterface.hpp>

#include <score/application/ApplicationContext.hpp>
#include <score/plugins/customfactory/FactoryFamily.hpp>

#include <QObject>
#include <utility>
#include <vector>

class score_addon_jit final :
        public score::Plugin_QtInterface,
        public score::FactoryInterface_QtInterface
{
  SCORE_PLUGIN_METADATA(1, "3be955fe-4a09-498b-834a-25df3b7a8ca5")

    public:
        score_addon_jit();
        virtual ~score_addon_jit() override;

    private:
        // Defined in FactoryInterface_QtInterface
        std::vector<std::unique_ptr<score::InterfaceBase>> factories(
                const score::ApplicationContext& ctx,
                const score::InterfaceKey& key) const override;

};
