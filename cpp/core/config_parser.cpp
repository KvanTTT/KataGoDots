#include "../core/config_parser.h"

#include "../core/fileutils.h"

#include <fstream>
#include <optional>
#include <sstream>

using namespace std;

ConfigParser::ConfigParser(bool keysOverride, bool keysOverrideFromIncludes_)
  :initialized(false),fileName(),contents(),keyValues(),
   keysOverrideEnabled(keysOverride),keysOverrideFromIncludes(keysOverrideFromIncludes_),
   curLineNum(0),curFilename(),includedFiles(),baseDirs(),logMessages(),
   usedKeysMutex(),usedKeys()
{}

ConfigParser::ConfigParser(const string& fname, bool keysOverride, bool keysOverrideFromIncludes_)
  :ConfigParser(keysOverride, keysOverrideFromIncludes_)
{
  initialize(fname);
}

ConfigParser::ConfigParser(const char* fname, bool keysOverride, bool keysOverrideFromIncludes_)
  :ConfigParser(std::string(fname), keysOverride, keysOverrideFromIncludes_)
{}

ConfigParser::ConfigParser(istream& in, bool keysOverride, bool keysOverrideFromIncludes_)
  :ConfigParser(keysOverride, keysOverrideFromIncludes_)
{
  initialize(in);
}

ConfigParser::ConfigParser(const map<string, string>& kvs)
  :ConfigParser(false, true)
{
  initialize(kvs);
}

ConfigParser::ConfigParser(const ConfigParser& source) {
  if(!source.initialized)
    throw StringError("Can only copy a ConfigParser which has been initialized.");
  std::lock_guard<std::mutex> lock(source.usedKeysMutex);
  initialized = source.initialized;
  fileName = source.fileName;
  baseDirs = source.baseDirs;
  contents = source.contents;
  keyValues = source.keyValues;
  keysOverrideEnabled = source.keysOverrideEnabled;
  keysOverrideFromIncludes = source.keysOverrideFromIncludes;
  usedKeys = source.usedKeys;
}

void ConfigParser::initialize(const string& fname) {
  if(initialized)
    throw StringError("ConfigParser already initialized, cannot initialize again");
  ifstream in;
  FileUtils::open(in,fname);
  fileName = fname;
  string baseDir = extractBaseDir(fname);
  if(!baseDir.empty())
    baseDirs.push_back(baseDir);
  initializeInternal(in);
  initialized = true;
}

void ConfigParser::initialize(istream& in) {
  if(initialized)
    throw StringError("ConfigParser already initialized, cannot initialize again");
  initializeInternal(in);
  initialized = true;
}

void ConfigParser::initialize(const map<string, string>& kvs) {
  if(initialized)
    throw StringError("ConfigParser already initialized, cannot initialize again");
  keyValues = kvs;
  initialized = true;
}

void ConfigParser::initializeInternal(istream& in) {
  keyValues.clear();
  contents.clear();
  curFilename = fileName;
  readStreamContent(in);
}

void ConfigParser::processIncludedFile(const std::string &fname) {
  if(fname == fileName || find(includedFiles.begin(), includedFiles.end(), fname) != includedFiles.end()) {
    throw ConfigParsingError("Circular or multiple inclusion of the same file: '" + fname + "'" + lineAndFileInfo());
  }
  includedFiles.push_back(fname);
  curFilename = fname;

  string fpath;
  for(const std::string& p: baseDirs) {
    fpath += p;
  }
  fpath += fname;

  string baseDir = extractBaseDir(fname);
  if(!baseDir.empty()) {
    if(baseDir[0] == '\\' || baseDir[0] == '/')
      throw ConfigParsingError("Absolute paths in the included files are not supported yet");
    baseDirs.push_back(baseDir);
  }

  ifstream in;
  FileUtils::open(in,fpath);
  readStreamContent(in);

  if(!baseDir.empty())
    baseDirs.pop_back();
}

bool ConfigParser::parseKeyValue(const std::string& trimmedLine, std::string& key, std::string& value) {
  // Parse trimmed line, taking into account comments and quoting.
  key.clear();
  value.clear();

  // Parse key
  bool foundAnyKey = false;
  size_t i = 0;
  for(; i<trimmedLine.size(); i++) {
    char c = trimmedLine[i];
    if(Global::isAlpha(c) || Global::isDigit(c) || c == '_' || c == '-') {
      key += c;
      foundAnyKey = true;
      continue;
    }
    else if(c == '#') {
      if(foundAnyKey)
        throw ConfigParsingError("Could not parse key value pair" + lineAndFileInfo());
      return false;
    }
    else if(Global::isWhitespace(c) || c == '=')
      break;
    else
      throw ConfigParsingError("Could not parse key value pair" + lineAndFileInfo());
  }
  // Skip whitespace after key
  for(; i<trimmedLine.size(); i++) {
    char c = trimmedLine[i];
    if(Global::isWhitespace(c))
      continue;
    else if(c == '#') {
      if(foundAnyKey)
        throw ConfigParsingError("Could not parse key value pair" + lineAndFileInfo());
      return false;
    }
    else if(c == '=') {
      break;
    }
    else
      throw ConfigParsingError("Could not parse key value pair" + lineAndFileInfo());
  }
  // Skip equals sign
  bool foundEquals = false;
  if(i < trimmedLine.size()) {
    assert(trimmedLine[i] == '=');
    foundEquals = true;
    i++;
  }
  // Skip whitespace after equals sign
  for(; i<trimmedLine.size(); i++) {
    char c = trimmedLine[i];
    if(Global::isWhitespace(c))
      continue;
    else if(c == '#') {
      if(foundAnyKey || foundEquals)
        throw ConfigParsingError("Could not parse key value pair" + lineAndFileInfo());
      return false;
    }
    else
      break;
  }

  // Maybe parse double quotes
  bool isDoubleQuotes = false;
  if(i < trimmedLine.size() && trimmedLine[i] == '"') {
    isDoubleQuotes = true;
    i++;
  }

  // Parse value
  bool foundAnyValue = false;
  for(; i<trimmedLine.size(); i++) {
    char c = trimmedLine[i];
    if(isDoubleQuotes) {
      if(c == '\\') {
        if(i+1 >= trimmedLine.size())
          throw ConfigParsingError("Could not parse key value pair" + lineAndFileInfo());
        i++;
        value += trimmedLine[i];
        foundAnyValue = true;
        continue;
      }
      else if(c == '"') {
        break;
      }
      else {
        value += c;
        foundAnyValue = true;
        continue;
      }
    }
    else {
      if(c == '#')
        break;
      else {
        value += c;
        foundAnyValue = true;
        continue;
      }
    }
  }

  if(isDoubleQuotes) {
    // Consume the trailing double quote
    if(i < trimmedLine.size() && trimmedLine[i] == '"')
      i++;
    else
      throw ConfigParsingError("Could not parse key value pair" + lineAndFileInfo());
    // The rest of the line can only be whitespace followed by a comment
    string remainder = Global::trim(trimmedLine.substr(i));
    if(remainder.size() > 0 && remainder[0] != '#')
      throw ConfigParsingError("Could not parse key value pair" + lineAndFileInfo());
  }
  else {
    // We stopped at a pound sign, the remainder is just comment or nothing
    // Trim whitespace off of unquoted values
    value = Global::trim(value);
  }

  if(isDoubleQuotes && !(foundAnyKey && foundAnyValue))
    throw ConfigParsingError("Could not parse key value pair" + lineAndFileInfo());
  if(foundEquals && !(foundAnyKey && foundAnyValue))
    throw ConfigParsingError("Could not parse key value pair" + lineAndFileInfo());
  if(foundAnyKey != foundAnyValue)
    throw ConfigParsingError("Could not parse key value pair" + lineAndFileInfo());

  return foundAnyKey;
}


void ConfigParser::readStreamContent(istream& in) {
  curLineNum = 0;
  string line;
  ostringstream contentStream;
  set<string> curFileKeys;
  while(getline(in,line)) {
    contentStream << line << "\n";
    curLineNum += 1;
    line = Global::trim(line);
    if(line.length() <= 0 || line[0] == '#')
      continue;

    if(line[0] == '@') {
      size_t commentPos = line.find("#");
      if(commentPos != string::npos)
        line = line.substr(0, commentPos);

      if(line.size() < 9) {
        throw ConfigParsingError("Unsupported @ directive" + lineAndFileInfo());
      }
      size_t pos0 = line.find_first_of(" \t\v\f=");
      if(pos0 == string::npos)
        throw ConfigParsingError("@ directive without value (key-val separator is not found)" + lineAndFileInfo());

      string key = Global::trim(line.substr(0,pos0));
      if(key != "@include")
        throw ConfigParsingError("Unsupported @ directive '" + key + "'" + lineAndFileInfo());

      string value = line.substr(pos0+1);
      size_t pos1 = value.find_first_not_of(" \t\v\f=");
      if(pos1 == string::npos)
        throw ConfigParsingError("@ directive without value (value after key-val separator is not found)" + lineAndFileInfo());

      value = Global::trim(value.substr(pos1));
      value = Global::trim(value, "'");  // remove single quotes for filename
      value = Global::trim(value, "\"");  // remove double quotes for filename

      int lineNum = curLineNum;
      processIncludedFile(value);
      curLineNum = lineNum;
      continue;
    }

    string key;
    string value;
    bool foundKeyValue = parseKeyValue(line, key, value);
    if(!foundKeyValue)
      continue;

    if(curFileKeys.find(key) != curFileKeys.end()) {
      if(!keysOverrideEnabled)
        throw ConfigParsingError("Key '" + key + "' + was specified multiple times in " +
                      curFilename + ", you probably didn't mean to do this, please delete one of them");
      else
        logMessages.push_back("Key '" + key + "' + was overriden by new value '" + value + "'" + lineAndFileInfo());
    }
    if(keyValues.find(key) != keyValues.end()) {
      if(!keysOverrideFromIncludes)
        throw ConfigParsingError("Key '" + key + "' + was specified multiple times in " +
                      curFilename + " or its included files, and key overriding is disabled");
      else
        logMessages.push_back("Key '" + key + "' + was overriden by new value '" + value + "'" + lineAndFileInfo());
    }
    keyValues[key] = value;
    curFileKeys.insert(key);
  }
  contents += contentStream.str();
}

string ConfigParser::lineAndFileInfo() const {
  return ", line " + Global::intToString(curLineNum) + " in '" + curFilename + "'";
}

string ConfigParser::extractBaseDir(const std::string &fname) {
  size_t slash = fname.find_last_of("/\\");
  if(slash != string::npos)
    return fname.substr(0, slash + 1);
  else
    return std::string();
}

ConfigParser::~ConfigParser()
{}

string ConfigParser::getFileName() const {
  return fileName;
}

string ConfigParser::getContents() const {
  return contents;
}

string ConfigParser::getAllKeyVals() const {
  ostringstream ost;
  for(auto it = keyValues.begin(); it != keyValues.end(); ++it) {
    ost << it->first + " = " + it->second << endl;
  }
  return ost.str();
}

void ConfigParser::unsetUsedKey(const string& key) {
  std::lock_guard<std::mutex> lock(usedKeysMutex);
  usedKeys.erase(key);
}

void ConfigParser::applyAlias(const string& mapThisKey, const string& toThisKey) {
  if(contains(mapThisKey) && contains(toThisKey))
    throw IOError("Cannot specify both " + mapThisKey + " and " + toThisKey + " in the same config");
  if(contains(mapThisKey)) {
    keyValues[toThisKey] = keyValues[mapThisKey];
    keyValues.erase(mapThisKey);
    std::lock_guard<std::mutex> lock(usedKeysMutex);
    if(usedKeys.find(mapThisKey) != usedKeys.end()) {
      usedKeys.insert(toThisKey);
      usedKeys.erase(mapThisKey);
    }
  }
}

void ConfigParser::overrideKey(const std::string& key, const std::string& value) {
  // Assume zero-length values mean to delete a key
  if(value.length() <= 0) {
    if(keyValues.find(key) != keyValues.end())
      keyValues.erase(key);
  }
  else
    keyValues[key] = value;
}

void ConfigParser::overrideKeys(const std::string& fname) {
  // It's a new config file, so baseDir is not relevant anymore
  baseDirs.clear();
  processIncludedFile(fname);
}

void ConfigParser::overrideKeys(const map<string, string>& newkvs) {
  for(auto iter = newkvs.begin(); iter != newkvs.end(); ++iter) {
    // Assume zero-length values mean to delete a key
    if(iter->second.length() <= 0) {
      if(keyValues.find(iter->first) != keyValues.end())
        keyValues.erase(iter->first);
    }
    else
      keyValues[iter->first] = iter->second;
  }
  fileName += " and/or command-line and query overrides";
}


void ConfigParser::overrideKeys(const map<string, string>& newkvs, const vector<pair<set<string>,set<string>>>& mutexKeySets) {
  for(size_t i = 0; i<mutexKeySets.size(); i++) {
    const set<string>& a = mutexKeySets[i].first;
    const set<string>& b = mutexKeySets[i].second;
    bool hasA = false;
    for(auto iter = a.begin(); iter != a.end(); ++iter) {
      if(newkvs.find(*iter) != newkvs.end()) {
        hasA = true;
        break;
      }
    }
    bool hasB = false;
    for(auto iter = b.begin(); iter != b.end(); ++iter) {
      if(newkvs.find(*iter) != newkvs.end()) {
        hasB = true;
        break;
      }
    }
    if(hasA) {
      for(auto iter = b.begin(); iter != b.end(); ++iter)
        keyValues.erase(*iter);
    }
    if(hasB) {
      for(auto iter = a.begin(); iter != a.end(); ++iter)
        keyValues.erase(*iter);
    }
  }

  overrideKeys(newkvs);
}

map<string,string> ConfigParser::parseCommaSeparated(const string& commaSeparatedValues) {
  map<string,string> keyValues;
  vector<string> pieces = Global::split(commaSeparatedValues,',');
  for(size_t i = 0; i<pieces.size(); i++) {
    string s = Global::trim(pieces[i]);
    if(s.length() <= 0)
      continue;
    size_t pos = s.find("=");
    if(pos == string::npos)
      throw ConfigParsingError("Could not parse kv pair, could not find '=' in:" + s);

    string key = Global::trim(s.substr(0,pos));
    string value = Global::trim(s.substr(pos+1));
    keyValues[key] = value;
  }
  return keyValues;
}

void ConfigParser::markKeyUsed(const string& key) {
  std::lock_guard<std::mutex> lock(usedKeysMutex);
  usedKeys.insert(key);
}

void ConfigParser::markAllKeysUsedWithPrefix(const string& prefix) {
  std::lock_guard<std::mutex> lock(usedKeysMutex);
  for(auto iter = keyValues.begin(); iter != keyValues.end(); ++iter) {
    const string& key = iter->first;
    if(Global::isPrefix(key,prefix))
      usedKeys.insert(key);
  }
}

void ConfigParser::warnUnusedKeys(ostream& out, Logger* logger) const {
  vector<string> unused = unusedKeys();
  vector<string> messages;
  if(unused.size() > 0) {
    messages.push_back("--------------");
    messages.push_back("WARNING: Config had unused keys! You may have a typo, an option you specified is being unused from " + fileName);
  }
  for(size_t i = 0; i<unused.size(); i++) {
    messages.push_back("WARNING: Unused key '" + unused[i] + "' in " + fileName);
  }
  if(unused.size() > 0) {
    messages.push_back("--------------");
  }

  if(logger) {
    for(size_t i = 0; i<messages.size(); i++)
      logger->write(messages[i]);
  }
  for(size_t i = 0; i<messages.size(); i++)
    out << messages[i] << endl;
}

vector<string> ConfigParser::unusedKeys() const {
  std::lock_guard<std::mutex> lock(usedKeysMutex);
  vector<string> unused;
  for(auto iter = keyValues.begin(); iter != keyValues.end(); ++iter) {
    const string& key = iter->first;
    if(usedKeys.find(key) == usedKeys.end())
      unused.push_back(key);
  }
  return unused;
}

bool ConfigParser::contains(const string& key) const {
  return keyValues.find(key) != keyValues.end();
}

bool ConfigParser::containsAny(const std::vector<std::string>& possibleKeys) const {
  for(const string& key : possibleKeys) {
    if(contains(key))
      return true;
  }
  return false;
}

std::string ConfigParser::firstFoundOrFail(const std::vector<std::string>& possibleKeys) const {
  for(const string& key : possibleKeys) {
    if(contains(key))
      return key;
  }
  string message = "Could not find key";
  for(const string& key : possibleKeys) {
    message += " '" + key + "'";
  }
  throw IOError(message + " in config file " + fileName);
}

std::string ConfigParser::firstFoundOrEmpty(const std::vector<std::string>& possibleKeys) const {
  for(const string& key : possibleKeys) {
    if(contains(key))
      return key;
  }
  return {};
}

std::string ConfigParser::getStringOrDefault(const std::string& key, const std::string& defaultValue, const std::set<std::string>& possibles) {
  string value;
  if (!tryGetString(key, value, possibles)) {
    value = defaultValue;
  }
  return value;
}

string ConfigParser::getString(const string& key, const set<string>& possibles) {
  string value;
  if (!tryGetString(key, value, possibles)) {
    throwNotFoundKeyException(key);
  }
  return value;
}

bool ConfigParser::tryGetString(const std::string& key, std::string& value, const std::set<std::string>& possibles) {
  const auto iter = keyValues.find(key);
  if(iter == keyValues.end()) {
    return false;
  }

  std::lock_guard lock(usedKeysMutex);
  usedKeys.insert(key);

  value = iter->second;

  validateValues(key, possibles, {value});

  return true;
}

vector<string> ConfigParser::getStrings(const string& key, const set<string>& possibles, const bool nonEmptyTrim) {
  vector<string> values = Global::split(getString(key),',');

  if (nonEmptyTrim) {
    vector<string> trimmedStrings;
    for(const auto& s : values) {
      if (string trimmed = Global::trim(s); !trimmed.empty()) {
        trimmedStrings.push_back(trimmed);
      }
    }
    values = trimmedStrings;
  }

  validateValues(key, possibles, values);

  return values;
}

void ConfigParser::validateValues(const string& key, const set<string>& possibles, const vector<string>& values) const {
  if(!possibles.empty()) {
    for(const auto& value: values) {
      if(possibles.find(value) == possibles.end())
        throw IOError(
          "Key '" + key + "' must be one of (" + Global::concat(possibles, "|") + ") in config file " + fileName);
    }
  }
}

bool ConfigParser::tryGetBool(const std::string& key, bool& value) {
  if (string str; tryGetString(key, str)) {
    value = parseOrError<bool>(key, str, false, true);
    return true;
  }
  return false;
}

bool ConfigParser::getBoolOrDefault(const std::string& key, const bool defaultValue) {
  return getOrError<bool>(key, false, true, defaultValue);
}

bool ConfigParser::getBool(const string& key) {
  return getOrError<bool>(key, false, true, std::nullopt);
}

bool ConfigParser::tryGetBools(const string& key, vector<bool>& values) {
  return getMultipleOrError(key, values, false, true, false);
}

vector<bool> ConfigParser::getBools(const string& key) {
  vector<bool> result;
  getMultipleOrError(key, result, false, true, true);
  return result;
}

bool ConfigParser::tryGetEnabled(const std::string& key, enabled_t& value) {
  if (string str; tryGetString(key, str)) {
    value = parseOrError<enabled_t>(key, str, enabled_t::False, enabled_t::Auto);
    return true;
  }
  return false;
}

enabled_t ConfigParser::getEnabledOrDefault(const string& key, const enabled_t defaultValue) {
  return getOrError<enabled_t>(key, enabled_t::False, enabled_t::Auto, defaultValue);
}

enabled_t ConfigParser::getEnabled(const string& key) {
  return getOrError<enabled_t>(key, enabled_t::False, enabled_t::Auto, std::nullopt);
}

int ConfigParser::getIntOrDefault(const std::string& key, const int min, const int max, const int defaultValue) {
  return getOrError<int>(key, min, max, defaultValue);
}

int ConfigParser::getInt(const string& key, const int min, const int max) {
  return getOrError<int>(key, min, max, std::nullopt);
}

bool ConfigParser::tryGetInt(const std::string& key, int& value, const int min, const int max) {
  if (string str; tryGetString(key, str)) {
    value = parseOrError<int>(key, str, min, max);
    return true;
  }
  return false;
}

bool ConfigParser::tryGetInts(const string& key, vector<int>& values, const int min, const int max) {
  return getMultipleOrError(key, values, min, max, false);
}

vector<int> ConfigParser::getInts(const string& key, const int min, const int max) {
  vector<int> result;
  getMultipleOrError(key, result, min, max, true);
  return result;
}

vector<std::pair<int,int>> ConfigParser::getNonNegativeIntDashedPairs(const string& key, const int min, const int max1, const int max2) {
  std::vector<string> pairStrs = getStrings(key);
  std::vector<std::pair<int,int>> ret;
  for(const string& pairStr: pairStrs) {
    if(Global::trim(pairStr).size() <= 0)
      continue;
    std::vector<string> pieces = Global::split(Global::trim(pairStr),'-');
    if(pieces.size() != 2) {
      throw IOError("Could not parse '" + pairStr + "' as a pair of integers separated by a dash for key '" + key + "' in config file " + fileName);
    }

    bool suc;
    int p0;
    int p1;
    suc = Global::tryStringToInt(pieces[0],p0);
    if(!suc)
      throw IOError("Could not parse '" + pairStr + "' as a pair of integers separated by a dash for key '" + key + "' in config file " + fileName);
    suc = Global::tryStringToInt(pieces[1],p1);
    if(!suc)
      throw IOError("Could not parse '" + pairStr + "' as a pair of integers separated by a dash for key '" + key + "' in config file " + fileName);

    if(p0 < min || p0 > max1 || p1 < min || p1 > max2)
      throw IOError("Expected key '" + key + "' to have all values range " + Global::intToString(min) + " to (" + Global::intToString(max1) + ", " + Global::intToString(max2) + ") in config file " + fileName);

    ret.push_back(std::make_pair(p0,p1));
  }
  return ret;
}

bool ConfigParser::tryGetInt64(const std::string& key, int64_t& value, const int64_t min, const int64_t max) {
  if (string str; tryGetString(key, str)) {
    value = parseOrError<int64_t>(key, str, min, max);
    return true;
  }
  return false;
}

int64_t ConfigParser::getInt64OrDefault(const std::string& key, const int64_t min, const int64_t max,
  const int64_t defaultValue) {
  return getOrError<int64_t>(key, min, max, defaultValue);
}

int64_t ConfigParser::getInt64(const string& key, const int64_t min, const int64_t max) {
  return getOrError<int64_t>(key, min, max, std::nullopt);
}

bool ConfigParser::tryGetInt64s(const string& key, vector<int64_t>& values, const int64_t min, const int64_t max) {
  return getMultipleOrError(key, values, min, max, false);
}

vector<int64_t> ConfigParser::getInt64s(const string& key, const int64_t min, const int64_t max) {
  vector<int64_t> result;
  getMultipleOrError(key, result, min, max, true);
  return result;
}

bool ConfigParser::tryGetUInt64(const std::string& key, uint64_t& value, const uint64_t min, const uint64_t max) {
  if (string str; tryGetString(key, str)) {
    value = parseOrError<uint64_t>(key, str, min, max);
    return true;
  }
  return false;
}

uint64_t ConfigParser::getUInt64OrDefault(const std::string& key,
  const uint64_t min, const uint64_t max,
  const uint64_t defaultValue) {
  return getOrError<uint64_t>(key, min, max, defaultValue);
}

uint64_t ConfigParser::getUInt64(const string& key, const uint64_t min, const uint64_t max) {
  return getOrError<uint64_t>(key, min, max, std::nullopt);
}

bool ConfigParser::tryGetUInt64s(const string& key, vector<uint64_t>& values, const uint64_t min, const uint64_t max) {
  return getMultipleOrError(key, values, min, max, false);
}

vector<uint64_t> ConfigParser::getUInt64s(const string& key, const uint64_t min, const uint64_t max) {
  vector<uint64_t> result;
  getMultipleOrError(key, result, min, max, true);
  return result;
}

bool ConfigParser::tryGetFloat(const std::string& key, float& value, const float min, const float max) {
  if (string str; tryGetString(key, str)) {
    value = parseOrError<float>(key, str, min, max);
    return true;
  }
  return false;
}

float ConfigParser::getFloatOrDefault(const string& key, const float min, const float max, float defaultValue) {
  return getOrError<float>(key, min, max, defaultValue);
}

float ConfigParser::getFloat(const std::string& key, const float min, const float max) {
  return getOrError<float>(key, min, max, std::nullopt);
}

bool ConfigParser::tryGetFloats(const string& key, vector<float>& values, const float min, const float max) {
  return getMultipleOrError(key, values, min, max, false);
}

vector<float> ConfigParser::getFloats(const string& key, const float min, const float max) {
  vector<float> result;
  getMultipleOrError(key, result, min, max, true);
  return result;
}

bool ConfigParser::tryGetDouble(const std::string& key, double& value, const double min, const double max) {
  if (string str; tryGetString(key, str)) {
    value = parseOrError<double>(key, str, min, max);
    return true;
  }
  return false;
}

double ConfigParser::getDouble(const std::string& key, const double min, const double max) {
  return getOrError<double>(key, min, max, std::nullopt);
}

double ConfigParser::getDoubleOrDefault(const string& key, const double min, const double max, double defaultValue) {
  return getOrError<double>(key, min, max, defaultValue);
}

bool ConfigParser::tryGetDoubles(const string& key, vector<double>& values, const double min, const double max) {
  return getMultipleOrError(key, values, min, max, false);
}

vector<double> ConfigParser::getDoubles(const string& key, const double min, const double max) {
  vector<double> result;
  getMultipleOrError(key, result, min, max, true);
  return result;
}

void ConfigParser::throwNotFoundKeyException(const string& key) const {
  throw IOError("Could not find key '" + key + "' in config file " + fileName);
}

template<typename T>
bool ConfigParser::getMultipleOrError(const string& key, vector<T>& values, const T min, const T max, const bool errorIfNotFound) {
  string str;
  if (!tryGetString(key, str)) {
    if (errorIfNotFound)
      throwNotFoundKeyException(key);
    return false;
  }

  vector<string> strings = Global::split(str,',');
  for(auto& s : strings) {
    values.push_back(parseOrError(key, s, min, max));
  }
  return true;
}

template<typename T>
T ConfigParser::getOrError(const string& key, const T min, const T max, const std::optional<T> defaultValue) {
  string foundStr;
  if (!tryGetString(key, foundStr)) {
    if (defaultValue.has_value()) {
      const auto value = defaultValue.value();
      if constexpr (std::is_arithmetic_v<T>) {
        assert(min <= max && value >= min && value <= max);
      }
      return value;
    }
    throwNotFoundKeyException(key);
  }

  return parseOrError(key, foundStr, min, max);
}

template<typename T>
T ConfigParser::parseOrError(const string& key, const string& str, const T min, const T max) {
  T x;
  bool success = false;
  if constexpr (std::is_same_v<T, int>) success = Global::tryStringToInt(str, x);
  else if constexpr (std::is_same_v<T, int64_t>) success = Global::tryStringToInt64(str, x);
  else if constexpr (std::is_same_v<T, uint64_t>) success = Global::tryStringToUInt64(str, x);
  else if constexpr (std::is_same_v<T, float>) success = Global::tryStringToFloat(str, x);
  else if constexpr (std::is_same_v<T, double>) success = Global::tryStringToDouble(str, x);
  else if constexpr (std::is_same_v<T, bool>) success = Global::tryStringToBool(str, x);
  else if constexpr (std::is_same_v<T, enabled_t>) success = enabled_t::tryParse(Global::toLower(str), x);

  if(!success)
    throw IOError("Could not parse '" + str + "' for key '" + key + "' in config file " + fileName);

  if constexpr (std::is_arithmetic_v<T>) {
    assert(min <= max);

    if constexpr (std::is_floating_point_v<T>) {
      if(std::isnan(x))
        throw IOError("Key '" + key + "' is nan in config file " + fileName);
    }

    if (x < min || x > max) {
      stringstream ss;
      ss << "Key '" << key << "' must be in the range " << min << " to " << max << " in config file " << fileName;
      throw IOError(ss.str());
    }
  }

  return x;
}