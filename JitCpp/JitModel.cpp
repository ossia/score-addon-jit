#include "JitModel.hpp"
#include <QVBoxLayout>
#include <QDialog>
#include <QPlainTextEdit>
#include <QDialogButtonBox>
//#include <JitCpp/Commands/EditJitEffect.hpp>

#include <score/command/Dispatchers/CommandDispatcher.hpp>
#include <score/tools/IdentifierGeneration.hpp>
#include <iostream>
#include <Process/Dataflow/PortFactory.hpp>

#include <ossia/dataflow/execution_state.hpp>

#include <../tools/driver/cc1_main.cpp>
namespace Jit
{

JitEffectModel::JitEffectModel(
    TimeVal t,
    const QString& jitProgram,
    const Id<Process::ProcessModel>& id,
    QObject* parent):
  Process::ProcessModel{t, id, "Jit", parent}
{
  init();
  setText(jitProgram);
}

JitEffectModel::~JitEffectModel()
{

}

void JitEffectModel::setText(const QString& txt)
{
  m_text = txt;
  reload();
}

void JitEffectModel::init()
{
}

QString JitEffectModel::prettyName() const
{
  return "Jit";
}
void JitEffectModel::reload()
{
  auto fx_text = m_text.toLocal8Bit();
  ossia::optional<jit_node> jit_factory;
  if(fx_text.isEmpty())
  {
    return;
  }
  try {
    jit_factory = ctx.compile(fx_text.toStdString());

    if(!jit_factory)
      return;
  } catch(...) {
    return;
  }

  std::unique_ptr<ossia::graph_node> jit_object{jit_factory->factory()};
  if(!jit_object)
  {
    jit_factory.reset();
    return;
  }

  if(node /*&& jit_object */)
  {
    /*
    // updating an existing DSP

    deleteCDSPInstance(jit_object); // TODO not thread-safe wrt exec thread
    deleteCDSPFactory(jit_factory);

    jit_factory = fac;
    jit_object = obj;
    // Try to reuse controls
    Jit::UpdateUI<decltype(*this)> ui{*this};
    buildUserInterfaceCDSPInstance(jit_object, &ui.glue);

    for(std::size_t i = ui.i; i < m_inlets.size(); i++)
    {
      controlRemoved(*m_inlets[i]);
      delete m_inlets[i];
    }
    m_inlets.resize(ui.i);
    */
  }
  else if(!m_inlets.empty() && !m_outlets.empty() && !jit_factory && !jit_object)
  {
    // loading
/*
    jit_factory = fac;
    jit_object = obj;
    // Try to reuse controls
    Jit::UpdateUI<decltype(*this)> ui{*this};
    buildUserInterfaceCDSPInstance(jit_object, &ui.glue);*/
  }
  else
  {
    // creating a new dsp

    node = std::move(jit_factory);
    qDeleteAll(m_inlets);
    qDeleteAll(m_outlets);
    m_inlets.clear();
    m_outlets.clear();

    struct inlet_vis {
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

      Process::Inlet* operator()() { return nullptr; }
    };

    struct outlet_vis {
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
      Process::Outlet* operator()() { return nullptr; }
    };
    for(ossia::inlet* port : jit_object->inputs())
    {
      if(auto inl = ossia::apply(inlet_vis{*this}, port->data))
      {
        m_inlets.push_back(inl);
      }
    }
    for(ossia::outlet* port : jit_object->outputs())
    {
      if(auto inl = ossia::apply(outlet_vis{*this}, port->data))
      {
        m_outlets.push_back(inl);
      }
    }

    if(!m_outlets.empty() && m_outlets.front()->type == Process::PortType::Audio)
      m_outlets.front()->setPropagate(true);
  }
}

JitEditDialog::JitEditDialog(const JitEffectModel& fx, const score::DocumentContext& ctx, QWidget* parent):
  QDialog{parent}
  , m_effect{fx}
{
  this->setWindowFlag(Qt::WindowCloseButtonHint, false);
  auto lay = new QVBoxLayout{this};
  this->setLayout(lay);

  m_textedit = new QPlainTextEdit{m_effect.text(), this};

  lay->addWidget(m_textedit);
  auto bbox = new QDialogButtonBox{
      QDialogButtonBox::Ok | QDialogButtonBox::Close, this};
  lay->addWidget(bbox);
  connect(bbox, &QDialogButtonBox::accepted,
          this, [&] {
    //CommandDispatcher<>{ctx.commandStack}.submitCommand(
    //      new Commands::EditJitEffect{fx, text()});
  });
  connect(bbox, &QDialogButtonBox::rejected,
          this, &QDialog::reject);
}

QString JitEditDialog::text() const
{
  return m_textedit->document()->toPlainText();
}


}

template <>
void DataStreamReader::read(
    const Jit::JitEffectModel& eff)
{
  readPorts(*this, eff.m_inlets, eff.m_outlets);
  m_stream << eff.m_text;
}

template <>
void DataStreamWriter::write(
    Jit::JitEffectModel& eff)
{
  writePorts(*this, components.interfaces<Process::PortFactoryList>(), eff.m_inlets, eff.m_outlets, &eff);

  m_stream >> eff.m_text;
  eff.reload();
}

template <>
void JSONObjectReader::read(
    const Jit::JitEffectModel& eff)
{
  readPorts(obj, eff.m_inlets, eff.m_outlets);
  obj["Text"] = eff.text();
}

template <>
void JSONObjectWriter::write(
    Jit::JitEffectModel& eff)
{
  writePorts(obj, components.interfaces<Process::PortFactoryList>(), eff.m_inlets, eff.m_outlets, &eff);
  eff.m_text = obj["Text"].toString();
  eff.reload();
}

namespace Process
{

template<>
QString EffectProcessFactory_T<Jit::JitEffectModel>::customConstructionData() const
{
  return R"_(
#include <vector>
#include <ossia/dataflow/graph_node.hpp>

struct foo : ossia::graph_node {
 foo()
 {
   m_inlets.push_back(ossia::make_inlet<ossia::value_port>());
   m_outlets.push_back(ossia::make_outlet<ossia::value_port>());
 }

 void run(ossia::token_request t, ossia::execution_state&)
 {
   std::cerr << " oh wow " << t.date.impl << std::endl;
   auto& in  = *m_inlets[0]->data.target<ossia::value_port>();
   auto& out = *m_outlets[0]->data.target<ossia::value_port>();

   for(auto& val : in.get_data())
   {
     out.add_raw_value(ossia::convert<float>(val.value) * 2);
   }
 }
};

extern "C" ossia::graph_node* score_graph_node_factory() {
  return new foo;
}
)_";
}

}
namespace Engine::Execution
{

Engine::Execution::JitEffectComponent::JitEffectComponent(
    Jit::JitEffectModel& proc,
    const Engine::Execution::Context& ctx,
    const Id<score::Component>& id,
    QObject* parent)
  : ProcessComponent_T{proc, ctx, id, "JitComponent", parent}
{
  if(proc.node)
  {
    this->node.reset(proc.node->factory());
    if(this->node)
    {
      m_ossia_process = std::make_shared<ossia::node_process>(node);
    }
  }
}





JitEffectComponent::~JitEffectComponent()
{

}

}
