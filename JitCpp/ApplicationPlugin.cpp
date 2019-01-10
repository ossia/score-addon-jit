#include <JitCpp/ApplicationPlugin.hpp>
#include <JitCpp/Jit.hpp>
#include <JitCpp/MetadataGenerator.hpp>

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
#include <QQuickWidget>
namespace Jit
{
ApplicationPlugin::ApplicationPlugin(const score::GUIApplicationContext& ctx)
  : score::GUIApplicationPlugin{ctx}
{
  con(m_addonsWatch, &QFileSystemWatcher::directoryChanged,
      this, [&] (const QString& a) { QTimer::singleShot(2000, [&] { setupAddon(a); }); });
  con(m_addonsWatch, &QFileSystemWatcher::fileChanged,
      this, &ApplicationPlugin::updateAddon);

  con(m_nodesWatch, &QFileSystemWatcher::fileChanged,
      this, &ApplicationPlugin::setupNode);

  con(m_compiler, &AddonCompiler::jobCompleted,
      this, &ApplicationPlugin::registerAddon, Qt::QueuedConnection);
}

void ApplicationPlugin::initialize()
{
  const auto& libpath = context.settings<Library::Settings::Model>().getPath();

  {
    auto nodes = libpath + "/Nodes";
    m_nodesWatch.addPath(nodes);

    QDirIterator it{nodes, QDir::Filter::Files | QDir::Filter::NoDotAndDotDot, QDirIterator::Subdirectories};
    while(it.hasNext())
    {
      auto path = it.next();
      m_nodesWatch.addPath(path);
      setupNode(path);
    }
  }

  {
    auto addons = libpath + "/Addons";
    m_addonsWatch.addPath(addons);
    QDirIterator it{addons, QDir::Filter::Dirs | QDir::Filter::NoDotAndDotDot, QDirIterator::NoIteratorFlags};
    while(it.hasNext())
    {
      it.next();
      setupAddon(it.fileInfo().filePath());
    }
  }

  delete new QQuickWidget;
}

void ApplicationPlugin::registerAddon(jit_plugin* p)
{
  auto plugin = p->plugin;

  qDebug() << "registerAddon => " << typeid(p->plugin).name();
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

  QSettings s;
  auto& settings = presenter->settings();
  for (auto& elt : settings_ifaces)
  {
    auto set = dynamic_cast<score::SettingsDelegateFactory*>(elt);
    SCORE_ASSERT(set);
    settings.setupSettingsPlugin(s, context, *set);
  }

  if (presenter->view())
  {
    for (auto& panel_fac : panels_ifaces)
    {
      auto p = static_cast<score::PanelDelegateFactory*>(panel_fac)->make(context);
      components.panels.push_back(std::move(p));
      presenter->view()->setupPanel(components.panels.back().get());
    }
  }

  qDebug() << "JIT addon registered" << p->plugin;
}

void ApplicationPlugin::setupAddon(const QString& addon)
{
  qDebug() << "Registering JIT addon" << addon;
  QFileInfo addonInfo{addon};
  auto addonFolderName = addonInfo.fileName();
  if(addonFolderName == "Nodes")
    return;

  auto [json, cpp_files, files] = loadAddon(addon);

  if(cpp_files.empty())
    return;

  auto addon_files_path = generateAddonFiles(addonFolderName, addon, files);
  std::vector<std::string> flags = {
    "-I" + addon.toStdString()
  , "-I" + addon_files_path.toStdString()};

  const std::string id = json["key"].toString().remove(QChar('-')).toStdString();
  m_compiler.submitJob(id, cpp_files, flags);
}

void ApplicationPlugin::setupNode(const QString& f)
{
  QFileInfo fi{f};
  if(fi.suffix() == "hpp" || fi.suffix() == "cpp")
  {
    if(QFile file{f}; file.open(QIODevice::ReadOnly))
    {
      auto node = file.readAll();
      constexpr auto make_uuid_s = "make_uuid";
      auto make_uuid = node.indexOf(make_uuid_s);
      if(make_uuid == -1)
        return;
      int umin = node.indexOf('"', make_uuid + 9);
      if(umin == -1)
        return;
      int umax = node.indexOf('"', umin + 1);
      if(umax == -1)
        return;
      if((umax - umin) != 37)
        return;
      auto uuid = QString{node.mid(umin + 1, 36)};
      uuid.remove(QChar('-'));

      node.append(
            R"_(
            #include <score/plugins/PluginInstances.hpp>

            SCORE_EXPORT_PLUGIN(Control::score_generic_plugin<Node>)
            )_");

      qDebug() << "Registering JIT node" << f;
      m_compiler.submitJob(uuid.toStdString(), node.toStdString(), {});
    }
  }
}

void ApplicationPlugin::updateAddon(const QString& f)
{
}


}
