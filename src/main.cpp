#include <stdlib.h>
#include <iostream>
#include <duckdb.h>
#include "piapiac.hpp"
#include "mqtt.hpp"

#include "quill/LogMacros.h"
#include "echidna/event_mgr.hpp"
#include "echidna/tcp.hpp"
#include "echidna/store.hpp"
#include "echidna/file.hpp"
#include "echidna/storeutil.hpp"

#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/Logger.h"
#include "quill/sinks/ConsoleSink.h"

using namespace coypu::event;
using namespace coypu::tcp;
using namespace coypu::store;
using namespace coypu::file;

// BEGIN Types
typedef std::function<void(void)> CBType;
typedef quill::Logger *LogType;
typedef coypu::event::EventManager<LogType> EventManagerType;
typedef coypu::store::LogRWStream<MMapAnon, coypu::store::OneShotCache, 128> AnonRWBufType;
typedef coypu::store::PositionedStream<AnonRWBufType> AnonStreamType;
// END Types

// TODO: Create mqtt manager and mqtt state machine
// TODO: On register, we have to send the connect message from the client.

typedef struct PiapiacContextS
{
  PiapiacContextS(LogType &consoleLogger) : _consoleLogger(consoleLogger)
  {
    _eventMgr = std::make_shared<EventManagerType>(consoleLogger);
    _set_write_ws = std::bind(&EventManagerType::SetWrite, _eventMgr, std::placeholders::_1);
    _mqttStreamSP = coypu::store::StoreUtil::CreateAnonStore<AnonStreamType, AnonRWBufType>(); // mqtt msgs will end up here
  }
  PiapiacContextS(const PiapiacContextS &other) = delete;
  PiapiacContextS &operator=(const PiapiacContextS &other) = delete;

  LogType _consoleLogger;

  std::shared_ptr<EventManagerType> _eventMgr;
  std::function<int(int)> _set_write_ws;
  std::shared_ptr<AnonStreamType> _mqttStreamSP;
} CoypuContext;

int main(int argc [[maybe_unused]], char **argv)
{
  if (!argv)
    exit(EXIT_FAILURE);

  quill::Logger *logger = quill::Frontend::create_or_get_logger(
      "root", quill::Frontend::create_or_get_sink<quill::ConsoleSink>("sink_id_1"));

  // set terminate
  std::set_terminate([]()
                     { std::cout << "Unhandled exception\n";
		std::abort(); });

  // Block all signals - wait til random is collected before blocking
  if (SignalFDHelper::BlockAllSignals() != 0)
  {
    LOG_ERROR(logger, "BlockAllSignals");
    exit(EXIT_FAILURE);
  }
  int opt;

  // start quill after
  quill::Backend::start();

  while ((opt = getopt(argc, argv, "d")) != -1)
  { // for each option...
    switch (opt)
    {
    case 'd':
      logger->set_log_level(quill::LogLevel::Debug);
      break;
    }
  }

  auto context = std::make_shared<CoypuContext>(logger);
  std::weak_ptr<CoypuContext> w_context = context;
  context->_eventMgr->Init();

  // BEGIN Signal - handle signals with fd
  sigset_t mask;
  ::sigemptyset(&mask);
  ::sigaddset(&mask, SIGINT);
  ::sigaddset(&mask, SIGKILL);
  ::sigaddset(&mask, SIGQUIT);
  int signalFD = SignalFDHelper::CreateNonBlockSignalFD(&mask);
  if (signalFD == -1)
  {
    LOG_ERROR(logger, "CreateNonBlockSignalFD {}", errno);
    exit(EXIT_FAILURE);
  }

  int mqttFD = TCPHelper::ConnectStream("mqtt", 1883);
  if (mqttFD == -1)
  {
    LOG_ERROR(logger, "ConnectStream {}", errno);

    exit(EXIT_FAILURE);
  }

  auto readCB = [&logger](int fd) -> int
  {
    ECHIDNA_LOG_INFO(logger, "piapiac readCB {}", fd);
    return 0;
  };
  auto writeCB = [&logger](int fd) -> int
  {
    ECHIDNA_LOG_INFO(logger, "piapiac writeCB {}", fd);
    return 0;
  };
  auto closeCB = [&logger](int fd) -> int
  {
    ECHIDNA_LOG_INFO(logger, "piapiac closeCB {}", fd);
    return 0;
  };

  if (context->_eventMgr->Register(mqttFD, readCB, writeCB, closeCB) != 0)
  {
    ECHIDNA_LOG_ERROR(logger, "Register {}", errno);
    exit(EXIT_FAILURE);
  }

  bool done = false;
  coypu::event::callback_type readSignalCB = [&done, &logger](int fd)
  {
    struct signalfd_siginfo signal;
    int count = ::read(fd, &signal, sizeof(signal));
    if (count == -1)
    {
      LOG_ERROR(logger, "readSignalCB");
    }
    done = true;
    ECHIDNA_LOG_WARNING(logger, "piapiac break");

    return 0;
  };

  ECHIDNA_LOG_INFO(logger, "piapiac starting");

  if (context->_eventMgr->Register(signalFD, readSignalCB, nullptr, nullptr))
  {
    LOG_ERROR(logger, "Register {}, errno");
  }
  // END signal

  // After a Network Connection is established by a Client to a Server, the first packet sent from the Client to the Server MUST be a CONNECT packet [MQTT-3.1.0-1].
  // need buffers, craete connect packet, then set write_ws flag
  //  context->_eventMgr->SetWrite(mqttFD);

  // unencrypted
  std::function<int(int, const struct iovec *, int)> readMQTTCB =
      std::bind(::writev, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
  std::function<int(int, const struct iovec *, int)> writeMQTTCB =
      std::bind(::readv, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

  //  contextSP->_mqttManager->RegisterConnection(mqttFD, false, readCB, writeCB, contextSP->_mqttSP);

  // wait til signal
  while (!done)
  {
    if (context->_eventMgr->Wait() < 0)
    {
      done = true;
    }
  }

  ECHIDNA_LOG_INFO(logger, "piapiac done");

  exit(EXIT_SUCCESS);
}
