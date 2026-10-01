#pragma once
#include <stddef.h>
#include <stdint.h>
enum class HttpResult { More, Ready, BadRequest, TooLarge };
// Fixed storage limits HTTP before JSON allocation. One request per connection.
class HttpRequest {
public:
  HttpResult feed(uint8_t);
  void reset();
  const char *method() const {return method_;}
  const char *path() const {return path_;}
  const char *body() const {return buffer_+headerSize_;}
  size_t bodySize() const {return contentLength_;}
private:
  char buffer_[2048+8192+1]{},method_[8]{},path_[64]{};
  size_t used_=0,headerSize_=0,contentLength_=0;
  bool parsed_=false;
  HttpResult result_=HttpResult::More;
  HttpResult parseHeaders();
};
