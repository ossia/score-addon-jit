#pragma once
#include <Process/Process.hpp>
#include <Effect/EffectFactory.hpp>
#include <Media/Effect/DefaultEffectItem.hpp>
#include <Media/Effect/EffectExecutor.hpp>
#include <Process/GenericProcessFactory.hpp>
#include <QDialog>
#include <JitCpp/Jit.hpp>
namespace Jit
{
class JitEffectModel;
}

PROCESS_METADATA(, Jit::JitEffectModel, "0a3b49d6-4ce7-4668-aec3-9505b6ee1a60", "Jit", "Jit", "Audio", {}, Process::ProcessFlags::ExternalEffect)
DESCRIPTION_METADATA(, Jit::JitEffectModel, "Jit")
namespace Jit
{
class JitEffectModel :
    public Process::ProcessModel
{
    friend class JitUI;
    friend class JitUpdateUI;
    Q_OBJECT
    SCORE_SERIALIZE_FRIENDS
    PROCESS_METADATA_IMPL(JitEffectModel)

    public:
      JitEffectModel(
        TimeVal t,
        const QString& jitProgram,
        const Id<Process::ProcessModel>&,
        QObject* parent);
    ~JitEffectModel() override;

    template<typename Impl>
    JitEffectModel(
        Impl& vis,
        QObject* parent) :
      Process::ProcessModel{vis, parent}
    {
      vis.writeTo(*this);
      init();
    }

    const QString& text() const
    {
      return m_text;
    }

    QString prettyName() const override;
    void setText(const QString& txt);

    Process::Inlets& inlets() { return m_inlets; }
    Process::Outlets& outlets() { return m_outlets; }

    jit_context ctx;
    ossia::optional<jit_node> node;
  private:
    void init();
    void reload();
    QString m_text;
};

}

namespace Process
{
template<>
QString EffectProcessFactory_T<Jit::JitEffectModel>::customConstructionData() const;
}
class QPlainTextEdit;
namespace Jit
{
struct JitEditDialog : public QDialog
{
    const JitEffectModel& m_effect;

    QPlainTextEdit* m_textedit{};
  public:
    JitEditDialog(const JitEffectModel& e, const score::DocumentContext& ctx, QWidget* parent);

    QString text() const;
};

using JitEffectFactory = Process::EffectProcessFactory_T<JitEffectModel>;
using LayerFactory = Process::EffectLayerFactory_T<JitEffectModel, Media::Effect::DefaultEffectItem, JitEditDialog>;

}


namespace Engine::Execution
{
class JitEffectComponent final
    : public Engine::Execution::ProcessComponent_T<Jit::JitEffectModel, ossia::node_process>
{
    Q_OBJECT
    COMPONENT_METADATA("122ceaeb-cbcc-4808-91f2-1929e3ca8292")

    public:
      static constexpr bool is_unique = true;

    JitEffectComponent(
        Jit::JitEffectModel& proc,
        const Engine::Execution::Context& ctx,
        const Id<score::Component>& id,
        QObject* parent);
    ~JitEffectComponent() override;
};
using JitEffectComponentFactory = Engine::Execution::ProcessComponentFactory_T<JitEffectComponent>;
}
