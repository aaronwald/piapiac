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
typedef coypu::store::LogRWStream<MMapShared, coypu::store::LRUCache, 128> RWBufType;
typedef coypu::event::EventManager<LogType> EventManagerType;
typedef coypu::store::LogRWStream<MMapAnon, coypu::store::OneShotCache, 128> AnonRWBufType;
typedef coypu::store::PositionedStream<AnonRWBufType> AnonStreamType;
typedef coypu::store::MultiPositionedStreamLog<RWBufType> PublishStreamType;

typedef eight99bushwick::piapiac::MqttManager<LogType, AnonStreamType, PublishStreamType> MqttManagerType;
// END Types

// TODO: Parse properties cleanly
// TODO: Parse publish msgs

typedef struct PiapiacContextS
{
  PiapiacContextS(LogType &consoleLogger) : _consoleLogger(consoleLogger)
  {
    _eventMgr = std::make_shared<EventManagerType>(consoleLogger);
    _set_write_ws = std::bind(&EventManagerType::SetWrite, _eventMgr, std::placeholders::_1);
    _mqttStreamSP = coypu::store::StoreUtil::CreateAnonStore<AnonStreamType, AnonRWBufType>(); // mqtt msgs will end up here

    _mqttManager = std::make_shared<MqttManagerType>(consoleLogger, _set_write_ws);

    _dm = std::make_shared<eight99bushwick::piapiac::DuckDBMgr>("./piapiac.duckdb");
  }
  PiapiacContextS(const PiapiacContextS &other) = delete;
  PiapiacContextS &operator=(const PiapiacContextS &other) = delete;

  LogType _consoleLogger;
  std::shared_ptr<eight99bushwick::piapiac::DuckDBMgr> _dm;

  std::shared_ptr<MqttManagerType> _mqttManager;
  std::shared_ptr<EventManagerType> _eventMgr;
  std::function<int(int)> _set_write_ws;
  std::shared_ptr<AnonStreamType> _mqttStreamSP;
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

  while ((opt = getopt(argc, argv, "dm:")) != -1)
  { // for each option...
    switch (opt)
    {
    case 'm':
      host = optarg;
      break;
    case 'd':
      logger->set_log_level(quill::LogLevel::Debug);
      break;
    }
  }

  auto context = std::make_shared<PiapiacContext>(logger);
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

  auto mqttCB = [w_context](uint16_t packet_id, const std::string &topic, const char *buf, uint32_t len)
  {
    std::shared_ptr<PiapiacContext> context = w_context.lock();
    if (context)
    {
      ECHIDNA_LOG_INFO(context->_consoleLogger, "received msg: {} -->[{}]", topic, std::string(buf, len));
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
  auto pingCB = [&mqttFD, &context](int fd [[maybe_unused]])
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
  dumpRecords(context);
  context->_dm->Destroy();

  exit(EXIT_SUCCESS);
}
