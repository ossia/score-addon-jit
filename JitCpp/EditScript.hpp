#pragma once

#include <score/command/Command.hpp>
#include <score/model/path/Path.hpp>
#include <JitCpp/JitModel.hpp>
#include <score/model/path/PathSerialization.hpp>

#include <QString>

namespace Jit
{
const CommandGroupKey& CommandFactoryName()
{
  static const CommandGroupKey key{"JS"};
  return key;
}

class EditScript final : public score::Command
{
  SCORE_COMMAND_DECL(Jit::CommandFactoryName(), EditScript, "Edit a C++ script")
public:
  EditScript(const JitEffectModel& model, const QString& text)
      : m_model{model}, m_new{text}
  {
    m_old = model.script();
  }

  void undo(const score::DocumentContext& ctx) const override
  {
    m_model.find(ctx).setScript(m_old);
  }

  void redo(const score::DocumentContext& ctx) const override
  {
    m_model.find(ctx).setScript(m_new);
  }

  void serializeImpl(DataStreamInput& s) const override
  {
    s << m_model << m_old << m_new;
  }

  void deserializeImpl(DataStreamOutput& s) override
  {
    s >> m_model >> m_old >> m_new;
  }
private:
  Path<JitEffectModel> m_model;
  QString m_old, m_new;
};
}
