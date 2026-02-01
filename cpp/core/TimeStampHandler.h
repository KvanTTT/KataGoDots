#ifndef CORE_TIME_STAMP_HANDLER_H
#define CORE_TIME_STAMP_HANDLER_H
#include "rand.h"

class TimeStampHandler {
public:
    explicit TimeStampHandler(Rand& rand);
    [[nodiscard]] std::string generateFileName(const std::string& path, const std::string& ext) const;
    [[nodiscard]] std::string getCurrentRandSeed() const;

private:
    std::string currentDateTime;
    uint64_t currentDateTimeRand;
    std::string currentRandSeed;
    std::string fileNameFriendlyRandSuffix;
};

#endif