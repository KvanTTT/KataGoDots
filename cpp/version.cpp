#include <sstream>
#include <string>
#include "core/global.h"
#include "version.h"
#include "game/board.h"
#include "program/gitinfo.h"

using namespace std;

string Version::getAppName() {
  return "KataGoDots";
}

string Version::getAppVersion() {
  return "1.16.4";
}

string Version::getBuildType() {
  return
#ifdef NDEBUG
  "Release"
#else
  "Debug"
#endif
  ;
}

string Version::getAppNameWithVersion() {
  return getAppName() + " " + getAppVersion();
}

string Version::getAppFullInfo(bool csv) {
  ostringstream out;

  auto commaOrWhitespace = [csv] {
    return csv ? "," : " ";
  };

  auto commaOrLineBreak = [csv] {
    return csv ? "," : "\n";
  };

  out << getAppName() << commaOrWhitespace() << getAppVersion();
  out << commaOrLineBreak();

  if (!csv) {
    out << "Git revision: ";
  }
  out << getGitRevision();
  out << commaOrLineBreak();

  if (!csv) {
    out << "Compile Time: ";
  }
  out << getCompilationDateTime(csv);
  out << commaOrLineBreak();

  if (!csv) {
    out << "Backend: ";
  }
  out << getBackend();
  out << commaOrLineBreak();

  if (!csv) {
#if defined(CUDA_TARGET_VERSION)
#define STRINGIFY(x) #x
#define STRINGIFY2(x) STRINGIFY(x)
    out << "Compiled with CUDA version " << STRINGIFY2(CUDA_TARGET_VERSION) << endl;
#endif

#if defined(USE_AVX2)
    out << "Compiled with AVX2 and FMA instructions" << endl;
#endif
#if defined(CACHE_TENSORRT_PLAN) && defined(USE_TENSORRT_BACKEND)
    out << "Compiled with TensorRT plan cache" << endl;
#elif defined(BUILD_DISTRIBUTED)
    out << "Compiled to support contributing to online distributed selfplay" << endl;
#endif
  }

  if (!csv) {
    out << "Compiled to allow boards of size up to ";
  }
  out << Board::MAX_LEN_X << commaOrWhitespace() << Board::MAX_LEN_Y;
  out << commaOrLineBreak();

  if (!csv) {
    out << "Build Type: ";
  }
  out << getBuildType();

  return out.str();
}

string Version::getGitRevision() {
  return GIT_REVISION;
}

string Version::getBackend() {
  return
#if defined(USE_CUDA_BACKEND)
  "CUDA"
#elif defined(USE_TENSORRT_BACKEND)
  "TensorRT"
#elif defined(USE_METAL_BACKEND)
  "Metal"
#elif defined(USE_OPENCL_BACKEND)
  "OpenCL"
#elif defined(USE_EIGEN_BACKEND)
  "Eigen"
#else
  "dummy"
#endif
  ;
}

string Version::getCompilationDateTime(const bool csv) {
  if (csv) {
    try {
      // Try to return date-time in the following format (completely numeric): YYYY-MM-DD-HH-MM-SS
      const vector<std::string> dateStrs = Global::split(__DATE__);
      const vector<std::string> timeStrs = Global::split(__TIME__, ':');

      const int year = Global::stringToInt(dateStrs[2]);

      static const vector<string> months = {
        "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
      };
      const size_t monthIndex = indexOf(months, dateStrs[0]);
      if (monthIndex == std::string::npos) throw std::runtime_error("Invalid month");

      const int day = Global::stringToInt(dateStrs[1]);

      const int hour = Global::stringToInt(timeStrs[0]);

      const int minute = Global::stringToInt(timeStrs[1]);

      const int second = Global::stringToInt(timeStrs[2]);

      std::string out;
      out.resize(19); // "YYYY-MM-DD-hh-mm-ss" = 19 chars
      std::snprintf(out.data(), out.size() + 1,
                    "%04d-%02d-%02d-%02d-%02d-%02d",
                    year, static_cast<int>(monthIndex) + 1, day, hour, minute, second);
      return out;
    } catch (const std::exception& _) {
    }
  }

  return string(__DATE__) + " " + string(__TIME__);
}
