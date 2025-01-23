#include <stdlib.h>
#include <iostream>
#include <duckdb.h>
#include "piapiac.hpp"
#include "mqtt.hpp"

#include "quill/LogMacros.h"
#include <echidna/event_mgr.hpp>

#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/Logger.h"
#include "quill/sinks/ConsoleSink.h"

using namespace coypu::event;

typedef std::function<void(void)> CBType;
typedef quill::Logger *LogType;
typedef coypu::event::EventManager<LogType> EventManagerType;

// TODO Wrap quill for libechidna
// TODO Connect to mqtt and parse for msg

typedef struct PiapiacContextS
{
	PiapiacContextS(LogType &consoleLogger) : _consoleLogger(consoleLogger)
	{
		_eventMgr = std::make_shared<EventManagerType>(consoleLogger);
		_set_write_ws = std::bind(&EventManagerType::SetWrite, _eventMgr, std::placeholders::_1);
	}
	PiapiacContextS(const PiapiacContextS &other) = delete;
	PiapiacContextS &operator=(const PiapiacContextS &other) = delete;

	LogType _consoleLogger;

	std::shared_ptr<EventManagerType> _eventMgr;
	std::function<int(int)> _set_write_ws;

	std::shared_ptr<EventCBManager<CBType>> _cbManager;

	std::unordered_map<int, std::pair<std::string, std::string>> _krakenChannelToPairType;
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

	// wait til signal
	while (!done)
	{
		if (context->_eventMgr->Wait() < 0)
		{
			done = true;
		}

		ECHIDNA_LOG_DEBUG(logger, "piapiac wait");
	}

	ECHIDNA_LOG_INFO(logger, "piapiac done");

	exit(EXIT_SUCCESS);
}
