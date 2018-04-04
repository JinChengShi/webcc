#ifndef CSOAP_HTTP_REQUEST_H_
#define CSOAP_HTTP_REQUEST_H_

#include <string>
#include "boost/asio/buffer.hpp"  // for const_buffer
#include "csoap/http_message.h"

namespace csoap {

class HttpRequest;

std::ostream& operator<<(std::ostream& os, const HttpRequest& request);

class HttpRequest : public HttpMessage {

  friend std::ostream& operator<<(std::ostream& os,
                                  const HttpRequest& request);

public:
  HttpRequest() = default;
  HttpRequest(const HttpRequest&) = default;
  HttpRequest& operator=(const HttpRequest&) = default;

  virtual ~HttpRequest() = default;

  const std::string& method() const {
    return method_;
  }

  void set_method(const std::string& method) {
    method_ = method;
  }

  const std::string& url() const {
    return url_;
  }

  void set_url(const std::string& url) {
    url_ = url;
  }

  const std::string& host() const {
    return host_;
  }

  const std::string& port() const {
    return port_;
  }

  // \param host Descriptive host name or numeric IP address.
  // \param port Numeric port number, "80" will be used if it's empty.
  void SetHost(const std::string& host, const std::string& port);

  // Compose start line from method, url, etc.
  void MakeStartLine();

  // Convert the response into a vector of buffers. The buffers do not own the
  // underlying memory blocks, therefore the request object must remain valid
  // and not be changed until the write operation has completed.
  // NOTE: Please call MakeStartLine() before.
  std::vector<boost::asio::const_buffer> ToBuffers() const;

private:
  // HTTP method.
  std::string method_;

  // Request URL.
  // A complete URL naming the requested resource, or the path component of
  // the URL.
  std::string url_;

  std::string host_;
  std::string port_;
};

}  // namespace csoap

#endif  // CSOAP_HTTP_REQUEST_H_
