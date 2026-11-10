#include <cassert>
#include <iostream>
#include "../../../Main_and_Cameras/SmartVest_SJKS01_Main/src/gsm/SimIdentity.h"

uint32_t clockMs=0;

static void operatorCases(){
 int mode=-1,access=-1; String name,code;
 assert(SimIdentity::parseOperator("+COPS: 0,0,\"GLOBE\",0",mode,name,code,access));
 assert(mode==0&&name=="Globe"&&code==""&&access==0);
 assert(SimIdentity::parseOperator("+COPS: 0,2,\"51503\",0",mode,name,code,access));
 assert(name=="Smart"&&code=="51503"&&access==0);
 assert(SimIdentity::parseOperator("+COPS: 0,2,\"51505\",0",mode,name,code,access));
 assert(name=="Sun / Smart"&&code=="51505");
 assert(SimIdentity::parseOperator("+COPS: 0",mode,name,code,access));
 assert(name=="Unavailable"&&access==-1);
 assert(!SimIdentity::parseOperator("+COPS: invalid",mode,name,code,access));
}

static void numberCases(){
 String number;
 assert(SimIdentity::parseSubscriberNumber("+CNUM: \"\",\"0917 123 4567\",129",number));
 assert(number=="+639171234567");
 assert(SimIdentity::parseSubscriberNumber("+CNUM: \"My SIM\",\"639181234567\",129",number));
 assert(number=="+639181234567");
 assert(!SimIdentity::parseSubscriberNumber("+CNUM: \"\",\"unknown\",129",number));
 assert(!SimIdentity::parseSubscriberNumber("OK",number));
}

static void cardCases(){
 String iccid;
 assert(SimIdentity::parseIccid("+CCID: 8963021234567890123",false,iccid));
 assert(iccid=="8963021234567890123");
 assert(SimIdentity::parseIccid("8963031234567890123",true,iccid));
 assert(!SimIdentity::parseIccid("1234",true,iccid));
 assert(SimIdentity::cmeCode("+CME ERROR: 10")==10);
 assert(SimIdentity::cmeCode("+CME ERROR: SIM not inserted")==-1);
 String carrier,code;
 assert(SimIdentity::parseImsi("515029123456789",true,carrier,code));
 assert(carrier=="Globe"&&code=="51502");
 assert(SimIdentity::parseImsi("515039123456789",true,carrier,code));
 assert(carrier=="Smart"&&code=="51503");
 assert(!SimIdentity::parseImsi("51503",true,carrier,code));
}

int main(){
 operatorCases(); numberCases(); cardCases();
 std::cout<<"SIM identity parser scenarios passed.\n";
}
