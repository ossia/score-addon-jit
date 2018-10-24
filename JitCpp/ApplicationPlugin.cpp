#include <JitCpp/ApplicationPlugin.hpp>
#include <JitCpp/Jit.hpp>

#include <Library/LibrarySettings.hpp>

#include <score/plugins/qt_interfaces/CommandFactory_QtInterface.hpp>
#include <score/plugins/qt_interfaces/FactoryFamily_QtInterface.hpp>
#include <score/plugins/qt_interfaces/FactoryInterface_QtInterface.hpp>
#include <score/plugins/qt_interfaces/GUIApplicationPlugin_QtInterface.hpp>
#include <score/plugins/panel/PanelDelegateFactory.hpp>

#include <core/application/ApplicationRegistrar.hpp>
#include <core/presenter/Presenter.hpp>
#include <core/view/Window.hpp>

#include <QApplication>
#include <QDir>
#include <QThread>
#include <QDirIterator>
#include <wobjectimpl.h>


namespace Jit
{


struct jit_plugin_context
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
  jit_plugin_context() : X{0, nullptr}, jit{*llvm::EngineBuilder().selectTarget()}
  {
  }

  jit_plugin compile(std::string sourceCode, const std::vector<std::string>& flags)
  {
    auto module = jit.compileModuleFromCpp(sourceCode, flags, context);
    if (!module)
      throw jit_error{module.takeError()};

    // Compile to machine code and link.
    jit.submitModule(std::move(*module));
    auto jitedFn = jit.getFunction<score::Plugin_QtInterface* ()>("plugin_instance");
    if (!jitedFn)
      throw jit_error{jitedFn.takeError()};

    llvm::outs().flush();

    return {std::move(*module), (*jitedFn)()};
  }

  llvm::PrettyStackTraceProgram X;
  llvm::LLVMContext context;
  SimpleOrcJit jit;
};

static jit_plugin_context ctx;
static std::list<jit_plugin> plugs;

AddonCompiler::AddonCompiler()
{
  connect(this, &AddonCompiler::submitJob, this, &AddonCompiler::on_job, Qt::QueuedConnection);
  this->moveToThread(&m_thread);
  m_thread.start();
}

AddonCompiler::~AddonCompiler()
{
  m_thread.exit(0);
  m_thread.wait();
}

void AddonCompiler::on_job(const std::string& cpp, const std::vector<std::string>& flags)
{
  auto plug = ctx.compile(cpp, flags);
  if(plug.plugin)
  {
    plugs.push_front(std::move(plug));
    jobCompleted(&plugs.front());
  }
}


ApplicationPlugin::ApplicationPlugin(const score::GUIApplicationContext& ctx)
  : score::GUIApplicationPlugin{ctx}
{
  con(m_addonsWatch, &QFileSystemWatcher::directoryChanged,
      this, &ApplicationPlugin::setupAddon);
  con(m_addonsWatch, &QFileSystemWatcher::fileChanged,
      this, &ApplicationPlugin::updateAddon);

  con(m_compiler, &AddonCompiler::jobCompleted,
      this, &ApplicationPlugin::registerAddon);
}

void ApplicationPlugin::generate_command_files(
    const QString& output
    , const QString& addon_path
    , const std::vector<std::pair<QString, QString>>& files)
{
  QRegularExpression decl("SCORE_COMMAND_DECL\\([A-Za-z_0-9,:<>\r\n\t ]*\\(\\)[A-Za-z_0-9,\"':<>\r\n\t ]*\\)");
  QRegularExpression decl_t("SCORE_COMMAND_DECL_T\\([A-Za-z_0-9,:<>\r\n\t ]*\\)");

  QString includes;
  QString commands;
  for(const auto& f : files)
  {
    {
      auto res = decl.globalMatch(f.second);
      while(res.hasNext())
      {
        auto match = res.next();
        if(auto txt = match.capturedTexts(); !txt.empty())
        {
          if(auto split = txt[0].split(","); split.size() > 1)
          {
            auto filename = f.first; filename.remove(addon_path + "/");
            includes += "#include <" + filename + ">\n";
            commands += split[1] + ",\n";
          }
        }
      }
    }
  }
  commands.remove(commands.length() - 2, 2);
  commands.push_back("\n");
  QDir{}.mkpath(output);
  auto out_name = QFileInfo{addon_path}.fileName().replace("-", "_");
  {
    QFile cmd_f{output + "/" + out_name + "_commands_files.hpp"};
    cmd_f.open(QIODevice::WriteOnly);
    cmd_f.write(includes.toUtf8());
    cmd_f.close();
  }
  {
    QFile cmd_f{output + "/" + out_name + "_commands.hpp"};
    cmd_f.open(QIODevice::WriteOnly);
    cmd_f.write(commands.toUtf8());
    cmd_f.close();
  }
}


W_OBJECT_IMPL(AddonCompiler)

void ApplicationPlugin::setupAddon(const QString& addon)
{
  std::string cpp_files;
  std::vector<std::pair<QString, QString>> files;
  QDirIterator it{addon, {"*.cpp", "*.hpp"}, QDir::Filter::Files | QDir::Filter::NoDotAndDotDot, QDirIterator::Subdirectories};

  while(it.hasNext())
  {
    if(QFile f(it.next()); f.open(QIODevice::ReadOnly))
    {
      QFileInfo fi{f};
      if(fi.suffix() == "cpp")
      {
        cpp_files.append("#include \""+ it.filePath().toStdString() + "\"\n");
      }

      files.push_back({fi.filePath(), f.readAll()});
    }
  }

  QString addon_name = it.fileInfo().dir().dirName().replace("-", "_");
  QByteArray addon_export = addon_name.toUpper().toUtf8();

  QString addon_files_path = QDir::tempPath() + "/score-tmp-build/" + addon_name;
  generate_command_files(addon_files_path, addon, files);
  QDir{}.mkpath(addon_files_path);
  {
    QFile export_file = addon_files_path + "/" + addon_name + "_export.h";
    export_file.open(QIODevice::WriteOnly);
    QByteArray export_data{
      "#ifndef " + addon_export + "_EXPORT_H\n"
      "#define " + addon_export + "_EXPORT_H\n"
          "#define " + addon_export + "_EXPORT __attribute__((visibility(\"default\")))\n"
          "#define " + addon_export + "_DEPRECATED [[deprecated]]\n"
      "#endif\n"
    };
    export_file.write(export_data);
    export_file.close();
  }
  std::vector<std::string> flags = {
    "-I" + addon.toStdString()
  , "-I" + addon_files_path.toStdString()};
  m_compiler.submitJob(cpp_files, flags);
}

void ApplicationPlugin::registerAddon(jit_plugin* p)
{
  auto plugin = p->plugin;
  auto presenter = qApp->findChild<score::Presenter*>();
  if(!presenter)
    return;
  auto& components = presenter->components();

  score::GUIApplicationRegistrar registrar{
      presenter->components(), context, presenter->menuManager(),
      presenter->toolbarManager(), presenter->actionManager()};

  if (auto i = dynamic_cast<score::FactoryList_QtInterface*>(plugin))
  {
    for (auto&& elt : i->factoryFamilies())
    {
      registrar.registerFactory(std::move(elt));
    }
  }

  std::vector<score::ApplicationPlugin*> ap;
  std::vector<score::GUIApplicationPlugin*> gap;
  if(auto i = dynamic_cast<score::ApplicationPlugin_QtInterface*>(plugin))
  {
    if (auto plug = i->make_applicationPlugin(context))
    {
      ap.push_back(plug);
      registrar.registerApplicationPlugin(plug);
    }

    if (auto plug = i->make_guiApplicationPlugin(context))
    {
      gap.push_back(plug);
      registrar.registerGUIApplicationPlugin(plug);
    }
  }

  if (auto commands_plugin = dynamic_cast<score::CommandFactory_QtInterface*>(plugin))
  {
    registrar.registerCommands(commands_plugin->make_commands());
  }

  ossia::small_vector<score::InterfaceBase*, 8> settings_ifaces;
  ossia::small_vector<score::InterfaceBase*, 8> panels_ifaces;
  if (auto factories_plugin = dynamic_cast<score::FactoryInterface_QtInterface*>(plugin))
  {
    for (auto& factory_family : registrar.components().factories)
    {
      ossia::small_vector<score::InterfaceBase*, 8> ifaces;
      const score::ApplicationContext& base_ctx = context;
      // Register core factories
      for (auto&& new_factory :
           factories_plugin->factories(base_ctx, factory_family.first))
      {
        ifaces.push_back(new_factory.get());
        factory_family.second->insert(std::move(new_factory));
      }

      // Register GUI factories
      for (auto&& new_factory :
           factories_plugin->guiFactories(context, factory_family.first))
      {
        ifaces.push_back(new_factory.get());
        factory_family.second->insert(std::move(new_factory));
      }

      if(dynamic_cast<score::SettingsDelegateFactoryList*>(factory_family.second.get()))
      {
        settings_ifaces = std::move(ifaces);
      }
      else if(dynamic_cast<score::PanelDelegateFactoryList*>(factory_family.second.get()))
      {
        panels_ifaces = std::move(ifaces);
      }
    }
  }

  for(auto plug : ap)
    plug->initialize();
  for(auto plug : gap)
    plug->initialize();

  // TODO setup settings

  if (presenter->view())
  {
    for (auto& panel_fac : panels_ifaces)
    {
      auto p = static_cast<score::PanelDelegateFactory*>(panel_fac)->make(context);
      components.panels.push_back(std::move(p));
      presenter->view()->setupPanel(components.panels.back().get());
    }
  }
}

void ApplicationPlugin::updateAddon(const QString& addon)
{

}

void ApplicationPlugin::initialize()
{
  const auto& libpath = context.settings<Library::Settings::Model>().getPath();
  auto addons = libpath + "/Addons";
  m_addonsWatch.addPath(addons);
  QDirIterator it{addons, QDir::Filter::Dirs | QDir::Filter::NoDotAndDotDot, QDirIterator::NoIteratorFlags};
  while(it.hasNext())
  {
    it.next();
    setupAddon(it.fileInfo().filePath());
  }
}

}
