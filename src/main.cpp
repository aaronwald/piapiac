#include <stdlib.h>
#include <iostream>
#include <duckdb.h>
#include <assert.h>

#include "quill/LogMacros.h"

#include "piapiac.hpp"
#include "mqtt.hpp"
#include "echidna/event_mgr.hpp"
#include "echidna/tcp.hpp"
#include "echidna/store.hpp"
#include "echidna/file.hpp"
#include "echidna/storeutil.hpp"
#include "echidna/http2.hpp"

#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/Logger.h"
#include "quill/sinks/ConsoleSink.h"
#include "proto/piapiac.pb.h"

using namespace coypu::event;
using namespace coypu::tcp;
using namespace coypu::store;
using namespace coypu::file;
using namespace piapiac::msg;

// BEGIN Types
typedef std::function<void(void)> CBType;
typedef quill::Logger *LogType;
typedef coypu::store::LogRWStream<MMapShared, coypu::store::LRUCache, 128> RWBufType;
typedef coypu::event::EventManager<LogType> EventManagerType;
typedef coypu::store::LogRWStream<MMapAnon, coypu::store::OneShotCache, 128> AnonRWBufType;
typedef coypu::store::PositionedStream<AnonRWBufType> AnonStreamType;
typedef coypu::store::MultiPositionedStreamLog<RWBufType> PublishStreamType;
typedef coypu::http2::HTTP2GRPCManager<LogType, AnonStreamType, PublishStreamType, piapiac::msg::PiaRequest, piapiac::msg::PiaMessage> HTTP2GRPCManagerType;
typedef std::unordered_map<int, std::shared_ptr<AnonStreamType>> TxtBufMapType;

typedef eight99bushwick::piapiac::MqttManager<LogType, AnonStreamType, PublishStreamType> MqttManagerType;
// END Types

// TODO: Parse mqtt properties (ignored for now)
typedef struct PiapiacContextS
{
  PiapiacContextS(LogType &consoleLogger, const std::string &grpcPath) : _consoleLogger(consoleLogger)
  {
    _eventMgr = std::make_shared<EventManagerType>(consoleLogger);
    _set_write_ws = std::bind(&EventManagerType::SetWrite, _eventMgr, std::placeholders::_1);
    _mqttStreamSP = coypu::store::StoreUtil::CreateAnonStore<AnonStreamType, AnonRWBufType>(); // mqtt msgs will end up here
    _mqttManager = std::make_shared<MqttManagerType>(consoleLogger, _set_write_ws);
    _dm = std::make_shared<eight99bushwick::piapiac::DuckDBMgr>("./piapiac.duckdb");
    _grpcManager = std::make_shared<HTTP2GRPCManagerType>(_consoleLogger, _set_write_ws, grpcPath);
    _txtBufs = std::make_shared<TxtBufMapType>();
  }
  PiapiacContextS(const PiapiacContextS &other) = delete;
  PiapiacContextS &operator=(const PiapiacContextS &other) = delete;

  LogType _consoleLogger;
  std::shared_ptr<eight99bushwick::piapiac::DuckDBMgr> _dm;

  std::shared_ptr<MqttManagerType> _mqttManager;
  std::shared_ptr<EventManagerType> _eventMgr;
  std::function<int(int)> _set_write_ws;
  std::shared_ptr<AnonStreamType> _mqttStreamSP;
  std::shared_ptr<HTTP2GRPCManagerType> _grpcManager;
  std::shared_ptr<TxtBufMapType> _txtBufs;

  // Stores
  std::shared_ptr<PublishStreamType> _publishStreamSP;
} PiapiacContext;

void setupDuckDB(std::shared_ptr<PiapiacContext> &context)
{

  // microsecond ts
  duckdb_result res;
  if (!context->_dm->Query("CREATE TABLE IF NOT EXISTS pia_msgs (ts TIMESTAMP, packet_id INT, topic VARCHAR, msg VARCHAR);", &res))
  {
    const char *error = duckdb_result_error(&res);
    ECHIDNA_LOG_ERROR(context->_consoleLogger, "Failed to create table [{}]", error);
    duckdb_destroy_result(&res);
    exit(EXIT_FAILURE);
  }
  duckdb_destroy_result(&res); // mem leak will be detected by sanitizer if we dont do this
}

void storeMqttMessage(std::shared_ptr<PiapiacContext> &context, uint16_t packet_id, const std::string &topic, const char *buf, uint32_t len)
{
  duckdb_result res;
  std::stringstream ss;
  ss << "INSERT INTO pia_msgs VALUES (CURRENT_TIMESTAMP, " << packet_id << ", '" << topic << "', '" << std::string(buf, len) << "');";
  if (!context->_dm->Query(ss.str().c_str(), &res))
  {
    const char *error = duckdb_result_error(&res);
    ECHIDNA_LOG_ERROR(context->_consoleLogger, "Failed to insert msg [{}]", error);
    duckdb_destroy_result(&res);
    exit(EXIT_FAILURE);
  }
  duckdb_destroy_result(&res);
}

void dumpRecords(std::shared_ptr<PiapiacContext> &context)
{
  duckdb_result result;

  if (!context->_dm->Query("SELECT * FROM pia_msgs", &result))
  {
    ECHIDNA_LOG_ERROR(context->_consoleLogger, "Failed to query [{}]", duckdb_result_error(&result));
    duckdb_destroy_result(&result);
    return;
  }

  // print the names of the result
  size_t row_count = duckdb_row_count(&result);
  size_t column_count = duckdb_column_count(&result);
  std::string header;
  for (size_t i = 0; i < column_count; i++)
  {
    header += std::string(duckdb_column_name(&result, i)) + ",";
  }
  ECHIDNA_LOG_INFO(context->_consoleLogger, "{}", header);
  // print the data of the result
  for (size_t row_idx = 0; row_idx < row_count; row_idx++)
  {
    std::string row;
    for (size_t col_idx = 0; col_idx < column_count; col_idx++)
    {
      char *val = duckdb_value_varchar(&result, col_idx, row_idx);
      row += std::string(val) + ",";
      duckdb_free(val);
    }
    ECHIDNA_LOG_INFO(context->_consoleLogger, "{}", row);
  }
  duckdb_destroy_result(&result);
}

template <typename ContextType>
void AcceptHTTP2Client(std::shared_ptr<ContextType> &context, int fd)
{
  std::weak_ptr<ContextType> wContextSP = context;
  struct sockaddr_in client_addr;
  ::memset(&client_addr, 0, sizeof(client_addr));
  socklen_t addrlen = sizeof(sockaddr_in);

  // using IP V4
  int clientfd = TCPHelper::AcceptNonBlock(fd, reinterpret_cast<struct sockaddr *>(&client_addr), &addrlen);
  if (TCPHelper::SetNoDelay(clientfd))
  {
  }

  ECHIDNA_LOG_INFO(context->_consoleLogger, "Accept http2 FD[{}]", clientfd);

  if (context)
  {
    std::shared_ptr<AnonStreamType> txtBuf = coypu::store::StoreUtil::CreateAnonStore<AnonStreamType, AnonRWBufType>();
    context->_txtBufs->insert(std::make_pair(clientfd, txtBuf));

    uint64_t init_offset = UINT64_MAX;
    context->_publishStreamSP->Register(clientfd, init_offset);

    std::function<int(int)> readCB = std::bind(&HTTP2GRPCManagerType::Read, context->_grpcManager, std::placeholders::_1);
    std::function<int(int)> writeCB = std::bind(&HTTP2GRPCManagerType::Write, context->_grpcManager, std::placeholders::_1);
    std::function<int(int)> closeCB = [wContextSP](int fd)
    {
      auto context = wContextSP.lock();
      if (context)
      {
        context->_publishStreamSP->Unregister(fd);
        context->_grpcManager->Unregister(fd);

        auto b = context->_txtBufs->find(fd);
        if (b != context->_txtBufs->end())
        {
          context->_txtBufs->erase(b);
        }
      }
      return 0;
    };

    std::function<int(int, const struct iovec *, int)> readvCB = [](int fd, const struct iovec *iovec, int c) -> int
    { return ::readv(fd, iovec, c); };
    std::function<int(int, const struct iovec *, int)> writevCB = [](int fd, const struct iovec *iovec, int c) -> int
    { return ::writev(fd, iovec, c); };
    bool b = context->_grpcManager->RegisterConnection(clientfd, true, readvCB, writevCB, nullptr, txtBuf, context->_publishStreamSP);
    assert(b);
    int r = context->_eventMgr->Register(clientfd, readCB, writeCB, closeCB);
    assert(r == 0);
  }
}
void perror(LogType logger, int errnum, const char *msg)
{
  char buf[1024] = {};
  ECHIDNA_LOG_ERROR(logger, "[{0}] ({1}): {2}", errnum, strerror_r(errnum, buf, 1024), msg);
}

int BindAndListen(LogType logger, const std::string &interface, uint16_t port)
{
  int sockFD = TCPHelper::CreateIPV4NonBlockSocket();
  if (sockFD < 0)
  {
    ECHIDNA_LOG_ERROR(logger, "CreateIPV4NonBlockSocket {}", errno);
    perror(logger, errno, "CreateIPV4NonBlockSocket");
    return -1;
  }

  if (TCPHelper::SetReuseAddr(sockFD) < 0)
  {
    perror(logger, errno, "SetReuseAddr");

    return -1;
  }
  struct sockaddr_in interface_in;
  int ret = TCPHelper::GetInterfaceIPV4FromName(interface.c_str(), interface.length(), interface_in);
  if (ret)
  {
    ECHIDNA_LOG_ERROR(logger, "GetInterfaceIPV4FromName interface[{}]", interface);
    perror(logger, errno, "GetInterfaceIPV4FromName");

    return -1;
  }

  struct sockaddr_in serv_addr = {}; // v4 family
  ::memset(&serv_addr, 0, sizeof(serv_addr));

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY; // interface_in.sin_addr.s_addr;
  serv_addr.sin_port = htons(port);

  ret = TCPHelper::BindIPV4(sockFD, &serv_addr);
  if (ret != 0)
  {
    ::close(sockFD);
    perror(logger, errno, "BindIPV4");
    return -1;
  }

  ret = TCPHelper::Listen(sockFD, 16);
  if (ret != 0)
  {
    perror(logger, errno, "Listen");
    return -1;
  }
  return sockFD;
}

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
  std::string host("mqtt");
  bool dump_records = false;

  while ((opt = getopt(argc, argv, "dm:b")) != -1)
  { // for each option...
    switch (opt)
    {
    case 'b':
      dump_records = true;
      break;
    case 'm':
      host = optarg;
      break;
    case 'd':
      logger->set_log_level(quill::LogLevel::Debug);
      break;
    }
  }

  std::string grpcPath = "/piapiac.msg.PiapiacService/Snap";
  auto context = std::make_shared<PiapiacContext>(logger, grpcPath);
  context->_publishStreamSP = coypu::store::StoreUtil::CreateRollingStore<PublishStreamType, RWBufType>("./data/db");
  assert(context->_publishStreamSP);

  std::weak_ptr<PiapiacContext> w_context = context;
  context->_eventMgr->Init();
  context->_dm->Init();
  setupDuckDB(context);

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

  // BEGIN Create HTTP2 service
  int sockFD = BindAndListen(logger, "lo", 8080);
  if (sockFD > 0)
  {
    coypu::event::callback_type acceptCB = [w_context](int fd)
    {
      auto context = w_context.lock();
      if (context)
      {
        AcceptHTTP2Client(context, fd);
      }
      else
      {
        ECHIDNA_LOG_ERROR(context->_consoleLogger, "context expired");
      }
      return 0;
    };

    if (context->_eventMgr->Register(sockFD, acceptCB, nullptr, nullptr))
    {
      ECHIDNA_LOG_ERROR(logger, "Register {}", errno);
    }
  }
  else
  {
    ECHIDNA_LOG_ERROR(logger, "Failed to create http2 fd");
    exit(EXIT_FAILURE);
  }
  // END HTTP2 service

  uint32_t msg_count = 0;

  // BEGIN grpc
  std::function<piapiac::msg::PiaMessage(piapiac::msg::PiaRequest &)> cb =
      [logger, &msg_count](piapiac::msg::PiaRequest &request [[maybe_unused]])
  {
    ECHIDNA_LOG_INFO(logger, "grpc request[{}]", request.DebugString());
    piapiac::msg::PiaMessage cMsg;
    cMsg.set_count(msg_count);
    cMsg.set_title("foo");
    return cMsg;
  };
  context->_grpcManager->SetRequestCB(cb);
  // END grpc

  // BEGIN mqtt
  int mqttFD = TCPHelper::ConnectStream(host.c_str(), 1883);
  if (mqttFD <= 0)
  {
    LOG_ERROR(logger, "ConnectStream host {} {} fd {}", host, errno, mqttFD);

    exit(EXIT_FAILURE);
  }

  auto closeCB = [&logger](int fd)
  {
    ECHIDNA_LOG_INFO(logger, "closeCB {}", fd);
    return 0;
  };

  // unencrypted
  std::function<int(int, const struct iovec *, int)> readMQTTCB =
      std::bind(::readv, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
  std::function<int(int, const struct iovec *, int)> writeMQTTCB =
      std::bind(::writev, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

  auto mqttCB = [w_context, &msg_count](uint16_t packet_id, const std::string &topic, const char *buf, uint32_t len)
  {
    std::shared_ptr<PiapiacContext> context = w_context.lock();
    if (context)
    {
      ECHIDNA_LOG_INFO(context->_consoleLogger, "received msg: {} -->[{}]", topic, std::string(buf, len));
      ++msg_count;
      storeMqttMessage(context, packet_id, topic, buf, len);
    }
  };

  context->_mqttManager->Register(mqttFD, readMQTTCB, writeMQTTCB, mqttCB, context->_mqttStreamSP, nullptr);

  std::function<int(int)> mqttReadCB = std::bind(&MqttManagerType::Read, context->_mqttManager, std::placeholders::_1);
  std::function<int(int)> mqttWriteCB = std::bind(&MqttManagerType::Write, context->_mqttManager, std::placeholders::_1);
  context->_eventMgr->Register(mqttFD, mqttReadCB, mqttWriteCB, closeCB);

  uint16_t keepAlive = 60; // seconds
  bool cleanSession = true;
  assert(context->_mqttManager->Connect(mqttFD, keepAlive, "wald123", "wald", "wald", cleanSession));
  assert(context->_mqttManager->Subscribe(mqttFD, "#"));

  // Create ping timer
  int timerFD = TimerFDHelper::CreateMonotonicNonBlock();
  TimerFDHelper::SetRelativeRepeating(timerFD, keepAlive, 0); // 10 seconds
  auto pingCB = [&mqttFD, &context](int fd)
  {
    uint64_t x;
    assert(::read(fd, &x, sizeof(uint64_t)) == sizeof(uint64_t));
    context->_mqttManager->Ping(mqttFD);
    return 0;
  };
  context->_eventMgr->Register(timerFD, pingCB, nullptr, nullptr);
  // END mqtt

  // wait til signal
  while (!done)
  {
    if (context->_eventMgr->Wait() < 0)
    {
      done = true;
    }
  }

  ECHIDNA_LOG_INFO(logger, "piapiac done");
  if (dump_records)
  {
    dumpRecords(context);
  }
  context->_dm->Destroy();

  exit(EXIT_SUCCESS);
}
