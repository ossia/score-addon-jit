#include <core/application/MinimalApplication.hpp>
#include <JitCpp/AddonCompiler.hpp>
#include <QFile>
void on_startup()
{
  {
    auto compiler = Jit::makeCustomCompiler("benchmark_main");
    auto res = compiler(R"_(
      #include <iostream>
      extern "C" [[dllexport]]
      void benchmark_main() {
        std::cout << "Hello world" << std::endl;
      }
    )_", {});
    res();
  }

  {
    auto compiler = Jit::makeCustomCompiler("benchmark_main");
    QFile f{"/tmp/bench.cpp"};
    f.open(QIODevice::ReadOnly);
    auto src = f.readAll().toStdString();

    auto res = compiler(src, { "-I/home/jcelerier/travail/kfr/include", "-D_ENABLE_EXTENDED_ALIGNED_STORAGE" });
    res();
  }

  qApp->exit(0);
}

int main(int argc, char** argv)
{
  QLocale::setDefault(QLocale::C);
  std::setlocale(LC_ALL, "C");
  score::MinimalApplication app(argc, argv);

  QMetaObject::invokeMethod(&app, on_startup, Qt::QueuedConnection);
  return app.exec();
}
