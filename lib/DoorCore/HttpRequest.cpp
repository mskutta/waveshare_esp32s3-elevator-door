#include "HttpRequest.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
void HttpRequest::reset(){used_=headerSize_=contentLength_=0;parsed_=false;result_=HttpResult::More;method_[0]=path_[0]=0;}
HttpResult HttpRequest::parseHeaders() {
  char *line=buffer_,*end=strstr(line,"\r\n");
  if(!end)return HttpResult::BadRequest;
  *end=0;char version[16],extra;
  if(sscanf(line,"%7s %63s %15s %c",method_,path_,version,&extra)!=3 ||
     (strcmp(version,"HTTP/1.1")&&strcmp(version,"HTTP/1.0")) || path_[0]!='/')return HttpResult::BadRequest;
  if(strcmp(method_,"GET")&&strcmp(method_,"POST")&&strcmp(method_,"PUT"))return HttpResult::BadRequest;
  bool lengthSeen=false,json=false;
  line=end+2;
  while(*line) {
    end=strstr(line,"\r\n");if(!end)return HttpResult::BadRequest;*end=0;
    char *colon=strchr(line,':');if(!colon)return HttpResult::BadRequest;*colon=0;
    for(char *p=line;*p;++p){if(!isalnum(static_cast<unsigned char>(*p))&&*p!='-')return HttpResult::BadRequest;*p=tolower(static_cast<unsigned char>(*p));}
    char *value=colon+1;while(*value==' '||*value=='\t')++value;
    if(!strcmp(line,"transfer-encoding"))return HttpResult::BadRequest;
    if(!strcmp(line,"content-length")) {
      if(lengthSeen)return HttpResult::BadRequest;
      lengthSeen=true;
      if(!*value)return HttpResult::BadRequest;
      for(char *p=value;*p;++p) {
        if(*p<'0'||*p>'9')return HttpResult::BadRequest;
        contentLength_=contentLength_*10+(*p-'0');if(contentLength_>8192)return HttpResult::TooLarge;
      }
    }
    if(!strcmp(line,"content-type"))json=!strncmp(value,"application/json",16) && (value[16]==0||value[16]==';');
    line=end+2;
  }
  if(!strcmp(method_,"PUT")&&(!lengthSeen||!contentLength_||!json))return HttpResult::BadRequest;
  if(!strcmp(method_,"GET")&&contentLength_)return HttpResult::BadRequest;
  if(!strcmp(method_,"POST")&&contentLength_)return HttpResult::BadRequest;
  return HttpResult::More;
}
HttpResult HttpRequest::feed(uint8_t b) {
  if(result_!=HttpResult::More)return result_;
  if(!b)return result_=HttpResult::BadRequest;
  if(used_==sizeof(buffer_)-1)return result_=HttpResult::TooLarge;
  buffer_[used_++]=static_cast<char>(b);buffer_[used_]=0;
  if(!parsed_) {
    if(used_>=4&&!memcmp(buffer_+used_-4,"\r\n\r\n",4)) {
      headerSize_=used_;parsed_=true;buffer_[used_-2]=0;
      result_=parseHeaders();
      if(result_!=HttpResult::More)return result_;
    } else if(used_>=2048)return result_=HttpResult::TooLarge;
  }
  if(parsed_&&used_==headerSize_+contentLength_)result_=HttpResult::Ready;
  return result_;
}
