#pragma once
#include <Arduino.h>
#include <cstdio>

namespace SimIdentity {
inline bool digits(const String& value,size_t minimum,size_t maximum){
 if(value.length()<minimum||value.length()>maximum)return false;
 for(size_t i=0;i<value.length();++i)if(value[i]<'0'||value[i]>'9')return false;
 return true;
}
inline String quoted(const String& text,int index){
 int start=-1;
 for(int found=0,i=0;i<int(text.length());++i){
  if(text[i]!='"')continue;
  if(start<0)start=i+1;
  else{if(found++==index)return text.substring(start,i);start=-1;}
 }
 return "";
}
inline String canonicalOperator(const String& reported,String& numericCode){
 String value=reported;value.trim();numericCode="";
 if(digits(value,5,6)){
  numericCode=value;
  if(value=="51502")return "Globe";
  if(value=="51503")return "Smart";
  if(value=="51505")return "Sun / Smart";
  return value;
 }
 String upper=value;upper.toUpperCase();
 if(upper.indexOf("GLOBE")>=0)return "Globe";
 if(upper.indexOf("SMART")>=0)return "Smart";
 if(upper.indexOf("SUN")>=0)return "Sun / Smart";
 return value.length()?value:String("Unavailable");
}
inline bool parseOperator(const String& text,int& mode,String& name,String& numericCode,int& accessTechnology){
 if(!text.startsWith("+COPS:"))return false;
 const String reported=quoted(text,0);
 int parsedMode=-1,format=-1,technology=-1;
 const int fields=std::sscanf(text.c_str(),"+COPS: %d,%d",&parsedMode,&format);
 if(fields<1||parsedMode<0||parsedMode>4)return false;
 mode=parsedMode;
 name=reported.length()?canonicalOperator(reported,numericCode):String("Unavailable");
 const int lastComma=text.lastIndexOf(',');
 if(lastComma>=0&&lastComma>text.lastIndexOf('"')&&std::sscanf(text.c_str()+lastComma+1,"%d",&technology)==1)accessTechnology=technology;
 else accessTechnology=-1;
 return true;
}
inline bool parseSubscriberNumber(const String& text,String& number){
 if(!text.startsWith("+CNUM:"))return false;
 String candidate=quoted(text,1);if(!candidate.length())candidate=quoted(text,0);candidate.trim();
 String cleaned;
 for(size_t i=0;i<candidate.length();++i){
  const char c=candidate[i];
  if((c>='0'&&c<='9')||(c=='+'&&!cleaned.length()))cleaned+=c;
  else if(c!=' '&&c!='-'&&c!='('&&c!=')')return false;
 }
 if(cleaned.startsWith("09")&&cleaned.length()==11)cleaned="+63"+cleaned.substring(1);
 else if(cleaned.startsWith("639")&&cleaned.length()==12)cleaned="+"+cleaned;
 const size_t count=cleaned.length()-(cleaned.startsWith("+")?1:0);
 if(count<7||count>15)return false;
 number=cleaned;return true;
}
inline bool parseIccid(const String& text,bool commandContext,String& iccid){
 String value;
 if(text.startsWith("+CCID:"))value=text.substring(6);
 else if(commandContext)value=text;
 else return false;
 value.trim();if(!digits(value,15,22))return false;iccid=value;return true;
}
inline bool parseImsi(const String& text,bool commandContext,String& carrier,String& operatorCode){
 if(!commandContext)return false;
 String value=text;value.trim();
 if(!digits(value,14,16))return false;
 const String plmn=value.substring(0,5);
 String canonicalCode;
 carrier=canonicalOperator(plmn,canonicalCode);
 operatorCode=canonicalCode.length()?canonicalCode:plmn;
 return true;
}
inline int cmeCode(const String& text){
 int code=-1;char extra=0;
 return std::sscanf(text.c_str(),"+CME ERROR: %d %c",&code,&extra)==1?code:-1;
}
} // namespace SimIdentity
