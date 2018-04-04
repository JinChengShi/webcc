#include "csoap/rest_server.h"

#if CSOAP_DEBUG_OUTPUT
#include <iostream>
#endif

#include "csoap/url.h"

namespace csoap {

////////////////////////////////////////////////////////////////////////////////

bool RestServiceManager::AddService(RestServicePtr service,
                                    const std::string& url) {
  assert(service);

  ServiceItem item(service, url);

  std::regex::flag_type flags = std::regex::ECMAScript | std::regex::icase;

  try {
    // Compile the regex.
    item.url_regex.assign(url, flags);

    service_items_.push_back(item);

    return true;

  } catch (std::regex_error& e) {
#if CSOAP_DEBUG_OUTPUT
    std::cout << e.what() << std::endl;
#endif
  }

  return false;
}

RestServicePtr RestServiceManager::GetService(
    const std::string& url,
    std::vector<std::string>* sub_matches) {
  assert(sub_matches != NULL);

  for (ServiceItem& item : service_items_) {
    std::smatch match;

    if (std::regex_match(url, match, item.url_regex)) {
      // Any sub-matches?
      // NOTE: Start from 1 because match[0] is the whole string itself.
      for (size_t i = 1; i < match.size(); ++i) {
        sub_matches->push_back(match[i].str());
      }

      return item.service;
    }
  }

  return RestServicePtr();
}

////////////////////////////////////////////////////////////////////////////////

bool RestRequestHandler::RegisterService(RestServicePtr service,
                                         const std::string& url) {
  return service_manager_.AddService(service, url);
}

HttpStatus::Enum RestRequestHandler::HandleSession(HttpSessionPtr session) {
  Url url(session->request().url());

  if (!url.IsValid()) {
    session->SetResponseStatus(HttpStatus::kBadRequest);
    session->SendResponse();
    return HttpStatus::kBadRequest;
  }

  std::vector<std::string> sub_matches;
  RestServicePtr service = service_manager_.GetService(url.path(), &sub_matches);
  if (!service) {
#if CSOAP_DEBUG_OUTPUT
    std::cout << "No service matches the URL: " << url.path() << std::endl;
#endif
    session->SetResponseStatus(HttpStatus::kBadRequest);
    session->SendResponse();
    return HttpStatus::kBadRequest;
  }

  // TODO: Error handling.
  std::string content;
  service->Handle(session->request().method(),
                  session->request().content(),
                  &content);

  session->SetResponseStatus(HttpStatus::kOK);
  session->SetResponseContent(kTextJsonUtf8,
                              content.length(),
                              std::move(content));
  session->SendResponse();

  return HttpStatus::kOK;
}

////////////////////////////////////////////////////////////////////////////////

RestServer::RestServer(unsigned short port, std::size_t workers)
    : HttpServer(port, workers)
    , rest_request_handler_(new RestRequestHandler()) {
  request_handler_ = rest_request_handler_;
}

RestServer::~RestServer() {
  request_handler_ = NULL;
  delete rest_request_handler_;
}

bool RestServer::RegisterService(RestServicePtr service,
                                 const std::string& url) {
  return rest_request_handler_->RegisterService(service, url);
}

}  // namespace csoap
