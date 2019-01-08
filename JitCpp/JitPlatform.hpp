#pragma once
#include <ciso646>
#include <vector>
#include <string>
#include <iostream>
#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QDebug>
#include <llvm/Support/Host.h>
#if __has_include(<llvm/Config/llvm-config-64.h>)
#include <llvm/Config/llvm-config-64.h>
#elif __has_include(<llvm/Config/llvm-config.h>)
#include <llvm/Config/llvm-config.h>
#endif

#if defined(__has_feature)
  #if __has_feature(address_sanitizer) && !defined(__SANITIZE_ADDRESS__)
    #define __SANITIZE_ADDRESS__ 1
  #endif
#endif
namespace Jit
{
static inline std::string locateSDK()
{
  auto appFolder = qApp->applicationDirPath();
#if defined(SCORE_DEPLOYMENT_BUILD)

#if defined(_WIN32)
  {
    QDir d{appFolder};
    d.cd("sdk");
    return d.absolutePath().toStdString();
  }
#elif defined(__linux__)
  {
    QDir d{appFolder};
    d.cdUp();
    d.cd("usr");
    return d.absolutePath().toStdString();
  }
#elif defined(__APPLE__)
  {
    QDir d{appFolder};
    d.cdUp();
    d.cd("Frameworks");
    d.cd("Score.Framework");
    return d.absolutePath().toStdString();
  }
#endif

#else
  return "/usr";
#endif
}

static inline void populateCompileOptions(std::vector<std::string>& args)
{
  args.push_back("-triple");
  args.push_back(llvm::sys::getDefaultTargetTriple());
  args.push_back("-target-cpu");
  args.push_back(llvm::sys::getHostCPUName().lower());

  args.push_back("-std=c++1z");
  args.push_back("-disable-free");
  args.push_back("-fdeprecated-macro");
  args.push_back("-fmath-errno");
  args.push_back("-fuse-init-array");

  //args.push_back("-mrelocation-model");
  //args.push_back("static");
  args.push_back("-mthread-model");
  args.push_back("posix");
  args.push_back("-masm-verbose");
  args.push_back("-mconstructor-aliases");
  args.push_back("-munwind-tables");

  args.push_back("-dwarf-column-info");
  args.push_back("-debugger-tuning=gdb");

  args.push_back("-fcxx-exceptions");
  args.push_back("-fno-use-cxa-atexit");

  args.push_back("-Ofast");
  // -Ofast stuff:
  args.push_back("-menable-no-infs");
  args.push_back("-menable-no-nans");
  args.push_back("-menable-unsafe-fp-math");
  args.push_back("-fno-signed-zeros");
  args.push_back("-mreassociate");
  args.push_back("-freciprocal-math");
  args.push_back("-fno-trapping-math");
  args.push_back("-ffp-contract=fast");
  args.push_back("-ffast-math");
  args.push_back("-ffinite-math-only");

  args.push_back("-mrelocation-model"); args.push_back("pic");
  args.push_back("-pic-level"); args.push_back("2");
  args.push_back("-pic-is-pie");

  // if fsanitize:
  args.push_back("-mrelax-all");
  args.push_back("-disable-llvm-verifier");
  args.push_back("-discard-value-names");
#if defined(__SANITIZE_ADDRESS__)
  args.push_back("-fsanitize=address,alignment,array-bounds,bool,builtin,enum,float-cast-overflow,float-divide-by-zero,function,integer-divide-by-zero,nonnull-attribute,null,pointer-overflow,return,returns-nonnull-attribute,shift-base,shift-exponent,signed-integer-overflow,unreachable,vla-bound,vptr,unsigned-integer-overflow,implicit-integer-truncation");
  args.push_back("-fsanitize-recover=alignment,array-bounds,bool,builtin,enum,float-cast-overflow,float-divide-by-zero,function,integer-divide-by-zero,nonnull-attribute,null,pointer-overflow,returns-nonnull-attribute,shift-base,shift-exponent,signed-integer-overflow,vla-bound,vptr,unsigned-integer-overflow,implicit-integer-truncation");
  args.push_back("-fsanitize-blacklist=/usr/lib/clang/7.0.0/share/asan_blacklist.txt");
  args.push_back("-fsanitize-address-use-after-scope");
  args.push_back("-mdisable-fp-elim");
#endif
  args.push_back("-fno-assume-sane-operator-new");
  args.push_back("-stack-protector"); args.push_back("0");
  args.push_back("-fexceptions");
  args.push_back("-faddrsig");

  args.push_back("-momit-leaf-frame-pointer");
  args.push_back("-vectorize-loops");
  args.push_back("-vectorize-slp");
}


static inline void populateDefinitions(std::vector<std::string>& args)
{
  args.push_back("-DASIO_STANDALONE=1");
  args.push_back("-DBOOST_MULTI_INDEX_ENABLE_INVARIANT_CHECKING");
  args.push_back("-DBOOST_MULTI_INDEX_ENABLE_SAFE_MODE");
  args.push_back("-DQT_CORE_LIB");
  args.push_back("-DQT_DISABLE_DEPRECATED_BEFORE=0x050800");
  args.push_back("-DQT_GUI_LIB");
  args.push_back("-DQT_NETWORK_LIB");
  args.push_back("-DQT_NO_KEYWORDS");
  args.push_back("-DQT_QML_LIB");
  args.push_back("-DQT_QUICK_LIB");
  args.push_back("-DQT_SERIALPORT_LIB");
  args.push_back("-DQT_STATICPLUGIN");
  args.push_back("-DQT_SVG_LIB");
  args.push_back("-DQT_WEBSOCKETS_LIB");
  args.push_back("-DQT_WIDGETS_LIB");
  args.push_back("-DRAPIDJSON_HAS_STDSTRING=1");
  args.push_back("-DSCORE_DEBUG");
  args.push_back("-DSCORE_LIB_BASE");
  args.push_back("-DSCORE_LIB_DEVICE");
  args.push_back("-DSCORE_LIB_INSPECTOR");
  args.push_back("-DSCORE_LIB_PROCESS");
  args.push_back("-DSCORE_LIB_STATE");
  args.push_back("-DSCORE_PLUGIN_AUTOMATION");
  args.push_back("-DSCORE_PLUGIN_CURVE");
  args.push_back("-DSCORE_PLUGIN_DEVICEEXPLORER");
  args.push_back("-DSCORE_PLUGIN_ENGINE");
  args.push_back("-DSCORE_PLUGIN_LIBRARY");
  args.push_back("-DSCORE_PLUGIN_LOOP");
  args.push_back("-DSCORE_PLUGIN_MAPPING");
  args.push_back("-DSCORE_PLUGIN_MEDIA");
  args.push_back("-DSCORE_PLUGIN_SCENARIO");
  //args.push_back("-DSCORE_STATIC_PLUGINS");
  args.push_back("-DTINYSPLINE_DOUBLE_PRECISION");
  args.push_back("-D_GNU_SOURCE");
  args.push_back("-D__STDC_CONSTANT_MACROS");
  args.push_back("-D__STDC_FORMAT_MACROS");
  args.push_back("-D__STDC_LIMIT_MACROS");
}

static inline auto getPotentialTriples()
{
  std::vector<QString> triples;
  triples.push_back(LLVM_DEFAULT_TARGET_TRIPLE);
  triples.push_back(LLVM_HOST_TRIPLE);
#if defined(__x86_64__)
  triples.push_back("x86_64-pc-linux-gnu");
#elif defined(__i686__)
  triples.push_back("i686-pc-linux-gnu");
#elif defined(__i586__)
  triples.push_back("i586-pc-linux-gnu");
#elif defined(__i486__)
  triples.push_back("i486-pc-linux-gnu");
#elif defined(__i386__)
  triples.push_back("i386-pc-linux-gnu");
#elif defined(__arm__)
  triples.push_back("armv8-none-linux-gnueabi");
  triples.push_back("armv8-pc-linux-gnueabi");
  triples.push_back("armv8-none-linux-gnu");
  triples.push_back("armv8-pc-linux-gnu");
  triples.push_back("armv7-none-linux-gnueabi");
  triples.push_back("armv7-pc-linux-gnueabi");
  triples.push_back("armv7-none-linux-gnu");
  triples.push_back("armv7-pc-linux-gnu");
  triples.push_back("armv6-none-linux-gnueabi");
  triples.push_back("armv6-pc-linux-gnueabi");
  triples.push_back("armv6-none-linux-gnu");
  triples.push_back("armv6-pc-linux-gnu");
#elif defined(__aarch64__)
  triples.push_back("aarch64-none-linux-gnueabi");
  triples.push_back("aarch64-pc-linux-gnueabi");
  triples.push_back("aarch64-none-linux-gnu");
  triples.push_back("aarch64-pc-linux-gnu");
#endif

  return triples;
}
/**
 * @brief populateIncludeDirs Add paths to the relevant parts of the sdk
 *
 * On windows :
 *
 * folder/score.exe
 * -resource-dir folder/sdk/clang/7.0.0             -> clang stuff
 * -internal-isystem folder/sdk/include/c++/v1      -> libc++
 * -internal-isystem folder/sdk/clang/7.0.0/include -> clang stuff
 * -internal-externc-isystem folder/sdk/include     -> Qt, mingw, ffmpeg, etc.
 * -I folder/sdk/include/score                      -> score headers (ossia folder should be in include)
 *
 * On mac :
 * score.app/Contents/macOS/score
 * -resource-dir score.app/Contents/Frameworks/Score.Framework/clang/7.0.0
 * -internal-isystem score.app/Contents/Frameworks/Score.Framework/include/c++/v1
 * -internal-isystem score.app/Contents/Frameworks/Score.Framework/clang/7.0.0/include
 * -internal-externc-isystem score.app/Contents/Frameworks/Score.Framework/include
 * -I score.app/Contents/Frameworks/Score.Framework/include/score
 *
 * On linux :
 * /tmp/.mount/usr/bin/score
 * -resource-dir /tmp/.mount/usr/lib/clang/7.0.0
 * -internal-isystem /tmp/.mount/usr/include/c++/v1
 * -internal-isystem /tmp/.mount/usr/lib/clang/7.0.0/include
 * -internal-externc-isystem /tmp/.mount/usr/include
 * -I /tmp/.mount/usr/include/score
 *
 * Generally :
 * -resource-dir $SDK_ROOT/lib/clang/7.0.0
 * -internal-isystem $SDK_ROOT/include/c++/v1
 * -internal-isystem $SDK_ROOT/lib/clang/7.0.0/include
 * -internal-externc-isystem $SDK_ROOT/include
 * -I $SDK_ROOT/include/score
 */
static inline void populateIncludeDirs(std::vector<std::string>& args)
{
  auto sdk = locateSDK();
  qDebug() << "SDK located: " << QString::fromStdString(sdk);
  args.push_back("-resource-dir");
  args.push_back(sdk + "/lib/clang/" SCORE_LLVM_VERSION);

  args.push_back("-internal-isystem");
  args.push_back(sdk + "/lib/clang/" SCORE_LLVM_VERSION "/include");

  auto include = [&] (const auto& path){ args.push_back("-I" + sdk + "/include/" + path); };

#if defined(_LIBCPP_VERSION)
  include("c++/v1");
#elif defined(_GLIBCXX_RELEASE)
  // Try to locate the correct libstdc++ folder
  // TODO these are only heuristics. how to make them better ?
  {
    const auto libstdcpp_major = QString::number(_GLIBCXX_RELEASE);
    QDir dir(QString::fromStdString(sdk));
    if(!dir.cd("include"))
      throw std::runtime_error("Unable to locate libstdc++");
    if(!dir.cd("c++"))
      throw std::runtime_error("Unable to locate libstdc++");

    QDirIterator cpp_it{dir};
    while(cpp_it.hasNext())
    {
      cpp_it.next();
      auto ver = cpp_it.fileName();
      if(!ver.isEmpty() && ver.startsWith(libstdcpp_major))
      {
        auto gcc = ver.toStdString();

        // e.g. /usr/include/c++/8.2.1
        include("c++/" + gcc);

        dir.cd(QString::fromStdString(gcc));
        for(auto& triple : getPotentialTriples())
        {
          if(dir.exists(triple))
          {
            // e.g. /usr/include/c++/8.2.1/x86_64-pc-linux-gnu
            include("c++/" + gcc + "/" + triple.toStdString());
            break;
          }
        }

        break;
      }
    }
  }
#endif

  include("x86_64-linux-gnu"); // #debian
  include(""); // /usr/include
  include("qt");
  include("qt/QtCore");
  include("qt/QtGui");
  include("qt/QtWidgets");
  include("qt/QtXml");
  include("qt/QtQml");
  include("qt/QtQuick");
  include("qt/QtQuickWidgets");
  include("qt/QtNetwork");
  include("qt/QtSvg");
  include("qt/QtSql");
  include("qt/QtOpenGL");
  include("qt/QtQuickControls2");
  include("qt/QtQuickShapes");
  include("qt/QtQuickTemplates2");
  include("qt/QtSerialBus");
  include("qt/QtSerialPort");

#if defined(SCORE_DEPLOYMENT_BUILD)
  include("score");
#else

  auto src_include_dirs = {
    "/API/OSSIA",
    "/API/3rdparty/variant/include",
    "/API/3rdparty/nano-signal-slot/include",
    "/API/3rdparty/spdlog/include",
    "/API/3rdparty/brigand/include",
    "/API/3rdparty/fmt/include",
    "/API/3rdparty/hopscotch-map/include",
    "/API/3rdparty/chobo-shl/include",
    "/API/3rdparty/frozen/include",
    "/API/3rdparty/bitset2",
    "/API/3rdparty/GSL/include",
    "/API/3rdparty/flat_hash_map",
    "/API/3rdparty/flat/include",
    "/API/3rdparty/readerwriterqueue",
    "/API/3rdparty/concurrentqueue",
    "/API/3rdparty/SmallFunction/smallfun/include",
    "/API/3rdparty/asio/asio/include",
    "/API/3rdparty/websocketpp",
    "/API/3rdparty/rapidjson/include",
    "/API/3rdparty/RtMidi17",
    "/API/3rdparty/oscpack",
    "/API/3rdparty/multi_index/include",
    "/API/3rdparty/verdigris/src",
    "/API/3rdparty/weakjack",
    "/base/lib",
    "/base/plugins/score-lib-state",
    "/base/plugins/score-lib-device",
    "/base/plugins/score-lib-process",
    "/base/plugins/score-lib-inspector",
    "/base/plugins/score-plugin-curve",
    "/base/plugins/score-plugin-engine",
    "/base/plugins/score-plugin-scenario",
    "/base/plugins/score-plugin-library",
    "/base/plugins/score-plugin-deviceexplorer",
    "/base/plugins/score-plugin-media"
  };

  for(auto path : src_include_dirs)
  {
    args.push_back("-I" + std::string(SCORE_ROOT_SOURCE_DIR) + path);
  }

  auto src_build_dirs = {
    "/.",
    "/base/lib",
    "/base/plugins/score-lib-state",
    "/base/plugins/score-lib-device",
    "/base/plugins/score-lib-process",
    "/base/plugins/score-lib-inspector",
    "/base/plugins/score-plugin-curve",
    "/base/plugins/score-plugin-engine",
    "/base/plugins/score-plugin-scenario",
    "/base/plugins/score-plugin-library",
    "/base/plugins/score-plugin-deviceexplorer",
    "/base/plugins/score-plugin-media",
    "/API/OSSIA"
  };

  for(auto path : src_build_dirs)
  {
    args.push_back("-I" + std::string(SCORE_ROOT_BINARY_DIR) + path);
  }
#endif
}

}
