#include "TimeStampHandler.h"

#include "datetime.h"
#include "fileutils.h"
#include "global.h"

using namespace std;

TimeStampHandler::TimeStampHandler(Rand &newRand) {
    currentDateTime = DateTime::getCompactDateTimeString();
    currentDateTimeRand = newRand.nextUInt64();
    currentRandSeed = Global::uint64ToHexString(currentDateTimeRand);
    fileNameFriendlyRandSuffix = currentRandSeed.substr(0, 6);
}

string TimeStampHandler::generateFileName(const string &path, const string &ext) const {
    // Append rand suffix to avoid overlapping between different machines
    const auto origFileNameWoExt = path + currentDateTime + "-" + fileNameFriendlyRandSuffix;
    int counter = 0;
    // Make sure there is no local file name overlapping
    auto fileNameWoExt = origFileNameWoExt;
    while (FileUtils::exists(fileNameWoExt + ext)) {
        counter++;
        fileNameWoExt = origFileNameWoExt + "-" + to_string(counter);
    }
    return fileNameWoExt + ext;
}

string TimeStampHandler::getCurrentRandSeed() const {
    return currentRandSeed;
}
