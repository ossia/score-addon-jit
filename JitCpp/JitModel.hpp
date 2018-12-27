#pragma once
#include <Media/Effect/DefaultEffectItem.hpp>
#include <Process/Process.hpp>
#include <Media/Effect/EffectExecutor.hpp>
#include <Process/GenericProcessFactory.hpp>
#include <Process/Process.hpp>
#include <Process/ProcessMetadata.hpp>

#include <ossia/dataflow/execution_state.hpp>
#include <ossia/dataflow/graph_node.hpp>

#include <ossia/dataflow/node_process.hpp>

#include <QDialog>

#include <Effect/EffectFactory.hpp>
#include <JitCpp/Jit.hpp>
namespace Jit
{
class JitEffectModel;
}

PROCESS_METADATA(
    , Jit::JitEffectModel
    , "0a3b49d6-4ce7-4668-aec3-9505b6ee1a60"
    , "Jit"
    , "Jit"
    , Process::ProcessCategory::Script
    , "Script"
    , "JIT compilation process"
    , "ossia score"
    , QStringList{}
    , {}
    , {}
    , Process::ProcessFlags::ExternalEffect)
namespace Jit
{
  struct jitted_node
  {
    std::unique_ptr<llvm::Module> module;
    std::function<ossia::graph_node*()> factory;
  };

  struct jitted_node_ctx
  {
    struct init
    {
      init()
      {

        using namespace llvm;

        sys::PrintStackTraceOnErrorSignal({});

        atexit(llvm_shutdown);
        InitializeNativeTarget();
        InitializeNativeTargetAsmPrinter();
        InitializeNativeTargetAsmParser();
      }
    } _init;
    jitted_node_ctx() : X{0, nullptr}, jit{*llvm::EngineBuilder().selectTarget()}
    {
    }

    jitted_node compile(std::string sourceCode, const std::vector<std::string>& additional_flags = {})
    {
      auto t0 = std::chrono::high_resolution_clock::now();
      auto module = jit.compileModuleFromCpp(sourceCode, additional_flags, context);
      if (!module)
        throw jit_error{module.takeError()};

      // Compile to machine code and link.
      jit.submitModule(std::move(*module));
      auto jitedFn
          = jit.getFunction<ossia::graph_node*()>("score_graph_node_factory");
      if (!jitedFn)
        throw jit_error{jitedFn.takeError()};

      auto t1 = std::chrono::high_resolution_clock::now();
      std::cerr << "\n\nBUILD DURATION: " << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() << " ms \n\n";
      llvm::outs().flush();
      return {std::move(*module), *jitedFn};
    }

    llvm::PrettyStackTraceProgram X;
    llvm::LLVMContext context;
    SimpleOrcJit jit;
  };

class JitEffectModel : public Process::ProcessModel
{
  friend class JitUI;
  friend class JitUpdateUI;
  SCORE_SERIALIZE_FRIENDS
  PROCESS_METADATA_IMPL(JitEffectModel)

public:
  JitEffectModel(
      TimeVal t, const QString& jitProgram, const Id<Process::ProcessModel>&,
      QObject* parent);
  ~JitEffectModel() override;

  template <typename Impl>
  JitEffectModel(Impl& vis, QObject* parent)
      : Process::ProcessModel{vis, parent}
  {
    vis.writeTo(*this);
    init();
  }

  const QString& text() const
  {
    return m_text;
  }

  static constexpr bool hasExternalUI()
  {
    return false;
  }
  QString prettyName() const noexcept override;
  void setText(const QString& txt);

  Process::Inlets& inlets()
  {
    return m_inlets;
  }
  Process::Outlets& outlets()
  {
    return m_outlets;
  }

  jitted_node_ctx ctx;
  ossia::optional<jitted_node> node;

private:
  void init();
  void reload();
  QString m_text;
};
}

namespace Process
{
template <>
QString
EffectProcessFactory_T<Jit::JitEffectModel>::customConstructionData() const;

template <>
Process::Descriptor
EffectProcessFactory_T<Jit::JitEffectModel>::descriptor(QString d) const;
}
class QPlainTextEdit;
namespace Jit
{
struct JitEditDialog : public QDialog
{
  const JitEffectModel& m_effect;

  QPlainTextEdit* m_textedit{};

public:
  JitEditDialog(
      const JitEffectModel& e, const score::DocumentContext& ctx,
      QWidget* parent);

  QString text() const;
};

using JitEffectFactory = Process::EffectProcessFactory_T<JitEffectModel>;
using LayerFactory = Process::EffectLayerFactory_T<
    JitEffectModel, Media::Effect::DefaultEffectItem, JitEditDialog>;
}

namespace Execution
{
class JitEffectComponent final : public Execution::ProcessComponent_T<
                                     Jit::JitEffectModel, ossia::node_process>
{
  COMPONENT_METADATA("122ceaeb-cbcc-4808-91f2-1929e3ca8292")

public:
  static constexpr bool is_unique = true;

  JitEffectComponent(
      Jit::JitEffectModel& proc, const Execution::Context& ctx,
      const Id<score::Component>& id, QObject* parent);
  ~JitEffectComponent() override;
};
using JitEffectComponentFactory
    = Execution::ProcessComponentFactory_T<JitEffectComponent>;
}
