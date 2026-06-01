#pragma once
#include <string>

namespace Version {
    std::string getAppName();
    std::string getAppVersion();
    std::string getAppNameWithVersion();
    std::string getAppFullInfo(bool csv = false);
    std::string getGitRevision();
    std::string getBackend();
    std::string getBuildType();
    std::string getCompilationDateTime(bool csv = false);
}
