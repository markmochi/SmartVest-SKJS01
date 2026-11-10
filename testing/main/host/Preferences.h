#pragma once
#include <map>
#include "Arduino.h"
#include <ArduinoJson.h>
ARDUINOJSON_BEGIN_PUBLIC_NAMESPACE
template<> struct Converter<String> {
 static void toJson(const String& source,JsonVariant target){target.set(source.v);}
 static String fromJson(JsonVariantConst source){return String(source.as<const char*>());}
 static bool checkJson(JsonVariantConst source){return source.is<const char*>();}
};
ARDUINOJSON_END_PUBLIC_NAMESPACE
ARDUINOJSON_BEGIN_PRIVATE_NAMESPACE
template<> struct Reader<String,void>:Reader<const char*,void>{explicit Reader(const String& source):Reader<const char*,void>(source.c_str()){} };
template<> struct Reader<const String,void>:Reader<const char*,void>{explicit Reader(const String& source):Reader<const char*,void>(source.c_str()){} };
ARDUINOJSON_END_PRIVATE_NAMESPACE
namespace MockNvs {
inline std::map<std::string,String> values;
inline bool failSettings=false;
inline unsigned writes=0;
}
class Preferences {
 public:
 bool begin(const char*,bool){return true;}
 String getString(const char* key,const char* fallback){auto entry=MockNvs::values.find(key);return entry==MockNvs::values.end()?String(fallback):entry->second;}
 size_t putString(const char* key,const String& value){++MockNvs::writes;if(MockNvs::failSettings&&std::string(key)=="config")return 0;MockNvs::values[key]=value;return value.length();}
};
