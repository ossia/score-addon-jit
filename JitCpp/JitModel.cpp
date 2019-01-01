#include "JitModel.hpp"
#include <JitCpp/EditScript.hpp>

#include <QDialog>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QPushButton>
//#include <JitCpp/Commands/EditJitEffect.hpp>

#include <Process/Dataflow/PortFactory.hpp>

#include <score/command/Dispatchers/CommandDispatcher.hpp>
#include <score/tools/IdentifierGeneration.hpp>

#include <ossia/dataflow/execution_state.hpp>
#include <ossia/dataflow/port.hpp>

#include <iostream>

#if __has_include(<../tools/driver/cc1_main.cpp>)
#include <../tools/driver/cc1_main.cpp>
#else
#include "cc1_main.cpp"
#endif

#include <wobjectimpl.h>
W_OBJECT_IMPL(Jit::JitEffectModel)
namespace Jit
{

JitEffectModel::JitEffectModel(
    TimeVal t, const QString& jitProgram, const Id<Process::ProcessModel>& id,
    QObject* parent)
    : Process::ProcessModel{t, id, "Jit", parent}
{
  init();
  setScript(jitProgram);
}

JitEffectModel::~JitEffectModel()
{
}

void JitEffectModel::setScript(const QString& txt)
{
  m_text = txt;
  reload();
}

void JitEffectModel::init()
{
}

QString JitEffectModel::prettyName() const noexcept
{
  return "Jit";
}


struct inlet_vis
{
  JitEffectModel& self;
  Process::Inlet* operator()(const ossia::audio_port& p)
  {
    auto i = new Process::Inlet{getStrongId(self.inlets()), &self};
    i->type = Process::PortType::Audio;
    return i;
  }

  Process::Inlet* operator()(const ossia::midi_port& p)
  {
    auto i = new Process::Inlet{getStrongId(self.inlets()), &self};
    i->type = Process::PortType::Midi;
    return i;
  }

  Process::Inlet* operator()(const ossia::value_port& p)
  {
    auto i = new Process::Inlet{getStrongId(self.inlets()), &self};
    i->type = Process::PortType::Message;
    return i;
  }

  Process::Inlet* operator()()
  {
    return nullptr;
  }
};

struct outlet_vis
{
  JitEffectModel& self;
  Process::Outlet* operator()(const ossia::audio_port& p)
  {
    auto i = new Process::Outlet{getStrongId(self.outlets()), &self};
    i->type = Process::PortType::Audio;
    return i;
  }

  Process::Outlet* operator()(const ossia::midi_port& p)
  {
    auto i = new Process::Outlet{getStrongId(self.outlets()), &self};
    i->type = Process::PortType::Midi;
    return i;
  }

  Process::Outlet* operator()(const ossia::value_port& p)
  {
    auto i = new Process::Outlet{getStrongId(self.outlets()), &self};
    i->type = Process::PortType::Message;
    return i;
  }
  Process::Outlet* operator()()
  {
    return nullptr;
  }
};

void JitEffectModel::reload()
{
  auto fx_text = m_text.toLocal8Bit();
  ossia::optional<jitted_node> jit_factory;
  if (fx_text.isEmpty())
    return;

  try
  {
    jit_factory = ctx.compile(fx_text.toStdString());

    if (!jit_factory)
      return;
  }
  catch (const std::exception& e)
  {
    errorMessage(e.what());
    return;
  }
  catch (...)
  {
    errorMessage("JIT error");
    return;
  }

  std::unique_ptr<ossia::graph_node> jit_object{jit_factory->factory()};
  if (!jit_object)
  {
    jit_factory.reset();
    return;
  }
  // creating a new dsp

  node = std::move(jit_factory);
  qDeleteAll(m_inlets);
  qDeleteAll(m_outlets);
  m_inlets.clear();
  m_outlets.clear();

  for (ossia::inlet* port : jit_object->inputs())
  {
    if (auto inl = ossia::apply(inlet_vis{*this}, port->data))
    {
      m_inlets.push_back(inl);
    }
  }
  for (ossia::outlet* port : jit_object->outputs())
  {
    if (auto inl = ossia::apply(outlet_vis{*this}, port->data))
    {
      m_outlets.push_back(inl);
    }
  }

  if (!m_outlets.empty()
      && m_outlets.front()->type == Process::PortType::Audio)
    m_outlets.front()->setPropagate(true);
}

JitEditDialog::JitEditDialog(
    const JitEffectModel& fx, const score::DocumentContext& ctx,
    QWidget* parent)
    : QDialog{parent}, m_effect{fx}
{
  this->setWindowFlag(Qt::WindowCloseButtonHint, false);
  auto lay = new QVBoxLayout{this};
  this->setLayout(lay);

  m_textedit = new QPlainTextEdit{m_effect.script(), this};
  m_error = new QPlainTextEdit{this};
  m_error->setReadOnly(true);
  m_error->setContextMenuPolicy(Qt::ContextMenuPolicy::CustomContextMenu);

  lay->addWidget(m_textedit);
  lay->addWidget(m_error);
  auto bbox = new QDialogButtonBox{
      QDialogButtonBox::Ok | QDialogButtonBox::Reset | QDialogButtonBox::Close, this};
  bbox->button(QDialogButtonBox::Ok)->setText(tr("Compile"));
  bbox->button(QDialogButtonBox::Reset)->setText(tr("Clear log"));
  connect(bbox->button(QDialogButtonBox::Reset), &QPushButton::clicked,
          this, [=] {
    m_error->clear();
  });
  lay->addWidget(bbox);
  connect(bbox, &QDialogButtonBox::accepted, this, [&] {
    CommandDispatcher<>{ctx.commandStack}.submit(
         new EditScript{fx, text()});
  });
  connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);

  con(fx, &JitEffectModel::errorMessage,
      this, [=] (const QString& txt) {
    m_error->setPlainText(txt);
  });
}

QString JitEditDialog::text() const
{
  return m_textedit->document()->toPlainText();
}

jitted_node_ctx::jitted_node_ctx()
  : X{0, nullptr}
  , jit{*llvm::EngineBuilder().selectTarget()}
{
}


jitted_node jitted_node_ctx::compile(std::string sourceCode, const std::vector<std::string>& additional_flags)
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

}

template <>
void DataStreamReader::read(const Jit::JitEffectModel& eff)
{
  readPorts(*this, eff.m_inlets, eff.m_outlets);
  m_stream << eff.m_text;
}

template <>
void DataStreamWriter::write(Jit::JitEffectModel& eff)
{
  writePorts(
        *this, components.interfaces<Process::PortFactoryList>(), eff.m_inlets,
        eff.m_outlets, &eff);

  m_stream >> eff.m_text;
  eff.reload();
}

template <>
void JSONObjectReader::read(const Jit::JitEffectModel& eff)
{
  readPorts(obj, eff.m_inlets, eff.m_outlets);
  obj["Text"] = eff.script();
}

template <>
void JSONObjectWriter::write(Jit::JitEffectModel& eff)
{
  writePorts(
        obj, components.interfaces<Process::PortFactoryList>(), eff.m_inlets,
        eff.m_outlets, &eff);
  eff.m_text = obj["Text"].toString();
  eff.reload();
}

namespace Process
{

template <>
QString
EffectProcessFactory_T<Jit::JitEffectModel>::customConstructionData() const
{
  return R"_(
#include <ossia/dataflow/data.hpp>
#include <ossia/dataflow/graph_node.hpp>
#include <ossia/dataflow/port.hpp>
#include <ossia/network/value/value_conversion.hpp>

#include <iostream>
#include <vector>

struct foo : ossia::graph_node {
 foo()
 {
   using namespace ossia;
   m_inlets.push_back(make_inlet<value_port>());
   m_outlets.push_back(make_outlet<value_port>());
 }

 void run(ossia::token_request t, ossia::exec_state_facade) noexcept override
 {
   std::cerr << " oh wow " << t.date.impl << std::endl;
   auto& in  = *m_inlets[0]->data.target<ossia::value_port>();
   auto& out = *m_outlets[0]->data.target<ossia::value_port>();

   for(auto& val : in.get_data())
   {
     out.write_value(ossia::convert<float>(val.value) * 2, t.offset);
   }
 }
};

extern "C" ossia::graph_node* score_graph_node_factory() {
  return new foo;
}
)_";
}


template <>
Process::Descriptor
EffectProcessFactory_T<Jit::JitEffectModel>::descriptor(QString d) const
{
  return Metadata<Descriptor_k, Jit::JitEffectModel>::get();
}

}
namespace Execution
{

Execution::JitEffectComponent::JitEffectComponent(
    Jit::JitEffectModel& proc, const Execution::Context& ctx,
    const Id<score::Component>& id, QObject* parent)
    : ProcessComponent_T{proc, ctx, id, "JitComponent", parent}
{
  if (proc.node)
  {
    this->node.reset(proc.node->factory());
    if (this->node)
    {
      m_ossia_process = std::make_shared<ossia::node_process>(node);
    }
  }
}

JitEffectComponent::~JitEffectComponent()
{
}
}
