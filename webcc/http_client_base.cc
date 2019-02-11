#include "webcc/http_client_base.h"

#include "boost/asio/connect.hpp"
#include "boost/asio/read.hpp"
#include "boost/asio/write.hpp"
#include "boost/date_time/posix_time/posix_time.hpp"

#include "webcc/logger.h"
#include "webcc/utility.h"

using boost::asio::ip::tcp;

namespace webcc {

HttpClientBase::HttpClientBase(std::size_t buffer_size)
    : buffer_(buffer_size == 0 ? kBufferSize : buffer_size),
      deadline_(io_context_),
      timeout_seconds_(kMaxReadSeconds),
      stopped_(false),
      timed_out_(false),
      error_(kNoError) {
}

void HttpClientBase::SetTimeout(int seconds) {
  if (seconds > 0) {
    timeout_seconds_ = seconds;
  }
}

bool HttpClientBase::Request(const HttpRequest& request,
                             std::size_t buffer_size) {
  io_context_.restart();

  response_.reset(new HttpResponse());
  response_parser_.reset(new HttpResponseParser(response_.get()));

  stopped_ = false;
  timed_out_ = false;
  error_ = kNoError;

  BufferResizer buffer_resizer(&buffer_, buffer_size);

  if ((error_ = Connect(request)) != kNoError) {
    return false;
  }

  if ((error_ = SendReqeust(request)) != kNoError) {
    return false;
  }

  if ((error_ = ReadResponse()) != kNoError) {
    return false;
  }

  return true;
}

Error HttpClientBase::DoConnect(const HttpRequest& request,
                                const std::string& default_port) {
  tcp::resolver resolver(io_context_);

  std::string port = request.port(default_port);

  boost::system::error_code ec;
  auto endpoints = resolver.resolve(tcp::v4(), request.host(), port, ec);

  if (ec) {
    LOG_ERRO("Host resolve error (%s): %s, %s.", ec.message().c_str(),
             request.host().c_str(), port.c_str());
    return kHostResolveError;
  }

  LOG_VERB("Connect to server...");

  // Use sync API directly since we don't need timeout control.
  SocketConnect(endpoints, &ec);

  // Determine whether a connection was successfully established.
  if (ec) {
    LOG_ERRO("Socket connect error (%s).", ec.message().c_str());
    Stop();
    return kEndpointConnectError;
  }

  LOG_VERB("Socket connected.");

  return kNoError;
}

Error HttpClientBase::SendReqeust(const HttpRequest& request) {
  LOG_VERB("HTTP request:\n%s", request.Dump(4, "> ").c_str());

  // NOTE:
  // It doesn't make much sense to set a timeout for socket write.
  // I find that it's almost impossible to simulate a situation in the server
  // side to test this timeout.

  boost::system::error_code ec;

  // Use sync API directly since we don't need timeout control.
  SocketWrite(request, &ec);

  if (ec) {
    LOG_ERRO("Socket write error (%s).", ec.message().c_str());
    Stop();
    return kSocketWriteError;
  }

  LOG_INFO("Request sent.");

  return kNoError;
}

Error HttpClientBase::ReadResponse() {
  LOG_VERB("Read response (timeout: %ds)...", timeout_seconds_);

  deadline_.expires_from_now(boost::posix_time::seconds(timeout_seconds_));
  DoWaitDeadline();

  Error error = kNoError;
  DoReadResponse(&error);

  if (error == kNoError) {
    LOG_VERB("HTTP response:\n%s", response_->Dump(4, "> ").c_str());
  }

  return error;
}

void HttpClientBase::DoReadResponse(Error* error) {
  boost::system::error_code ec = boost::asio::error::would_block;

  auto read_handler = [this, &ec, error](boost::system::error_code inner_ec,
                                         std::size_t length) {
    ec = inner_ec;

    LOG_VERB("Socket async read handler.");

    if (ec || length == 0) {
      Stop();
      *error = kSocketReadError;
      LOG_ERRO("Socket read error (%s).", ec.message().c_str());
      return;
    }

    LOG_INFO("Read data, length: %u.", length);

    // Parse the response piece just read.
    if (!response_parser_->Parse(buffer_.data(), length)) {
      Stop();
      *error = kHttpError;
      LOG_ERRO("Failed to parse HTTP response.");
      return;
    }

    if (response_parser_->finished()) {
      // Stop trying to read once all content has been received, because
      // some servers will block extra call to read_some().
      Stop();

      LOG_INFO("Finished to read and parse HTTP response.");

      return;
    }

    if (!stopped_) {
      DoReadResponse(error);
    }
  };

  SocketAsyncReadSome(buffer_, read_handler);

  // Block until the asynchronous operation has completed.
  do {
    io_context_.run_one();
  } while (ec == boost::asio::error::would_block);
}

void HttpClientBase::DoWaitDeadline() {
  deadline_.async_wait(
      std::bind(&HttpClientBase::OnDeadline, this, std::placeholders::_1));
}

void HttpClientBase::OnDeadline(boost::system::error_code ec) {
  if (stopped_) {
    return;
  }

  LOG_VERB("OnDeadline.");

  // NOTE: Can't check this:
  //   if (ec == boost::asio::error::operation_aborted) {
  //     LOG_VERB("Deadline timer canceled.");
  //     return;
  //   }

  if (deadline_.expires_at() <=
      boost::asio::deadline_timer::traits_type::now()) {
    // The deadline has passed.
    // The socket is closed so that any outstanding asynchronous operations
    // are canceled.
    LOG_WARN("HTTP client timed out.");
    timed_out_ = true;
    Stop();
    return;
  }

  // Put the actor back to sleep.
  DoWaitDeadline();
}

void HttpClientBase::Stop() {
  if (stopped_) {
    return;
  }

  stopped_ = true;

  LOG_INFO("Close socket...");

  boost::system::error_code ec;
  SocketClose(&ec);

  if (ec) {
    LOG_ERRO("Socket close error (%s).", ec.message().c_str());
  }

  LOG_INFO("Cancel deadline timer...");
  deadline_.cancel();
}

}  // namespace webcc