
#pragma once

#include <arpa/inet.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <sys/uio.h>
#include <assert.h>
#include <echidna/buf.hpp>
#include <echidna/log.hpp>

namespace eight99bushwick::piapiac
{
  const uint8_t MQTT_PROTOCOL_VERSION = 5;
  struct MqttFixed
  {
    uint8_t flags : 4;
    uint8_t control_packet_type : 4;
  };
  // followed by variable length
  // followed by packet identifier (sometimes)

  enum class MqttControlPacketType : uint8_t
  {
    MQ_CPT_RESERVED = 0,
    MQ_CPT_CONNECT = 1,
    MQ_CPT_CONNACK = 2,
    MQ_CPT_PUBLISH = 3,
    MQ_CPT_PUBACK = 4,
    MQ_CPT_PUBREC = 5,
    MQ_CPT_PUBREL = 6,
    MQ_CPT_PUBCOMP = 7,
    MQ_CPT_SUBSCRIBE = 8,
    MQ_CPT_SUBACK = 9,
    MQ_CPT_UNSUBSCRIBE = 10,
    MQ_CPT_UNSUBACK = 11,
    MQ_CPT_PINGREQ = 12,
    MQ_CPT_PINGRESP = 13,
    MQ_CPT_DISCONNECT = 14,
    MQ_CPT_AUTH = 15
  };

  enum class MqttFlags : uint8_t
  {
    MQ_FLAG_PUBLISH_DUP = 0x08,
    MQ_FLAG_PUBLISH_QOS2 = 0x04,
    MQ_FLAG_PUBLISH_QOS1 = 0x02,
    MQ_FLAG_PUBLISH_RETAIN = 0x01,
  };

  enum class MqttSubscribeOptions : uint8_t
  {
    MQ_SO_MAX_QOS0 = 0x01,
    MQ_SO_MAX_QOS1 = 0x02,
    MQ_SO_NO_LOCAL = 0x04,
    MQ_SO_NO_RETAIN = 0x08,
    MQ_SO_RETAIN_B0 = 0x10,
    MQ_SO_RETAIN_B1 = 0x20,
    MQ_SO_RESERVED0 = 0x40,
    MQ_SO_RESERVED1 = 0x80
  };

  inline MqttFlags operator|(MqttFlags a, MqttFlags b)
  {
    return static_cast<MqttFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
  }

  enum class MqttConnectFlags : uint8_t
  {
    MQ_CF_USER_NAME_FLAG = 0x80,
    MQ_CF_PASSWORD_FLAG = 0x40,
    MQ_CF_WILL_RETAIN = 0x20,
    MQ_CF_WILL_QOS2 = 0x10,
    MQ_CF_WILL_QOS1 = 0x08,
    MQ_CF_WILL_FLAG = 0x04,
    MQ_CF_CLEAN_SESSION = 0x02,
    MQ_CF_RESERVED = 0x01
  };

  inline MqttConnectFlags operator|(MqttConnectFlags a, MqttConnectFlags b)
  {
    return static_cast<MqttConnectFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
  }

  enum class MqttState : uint8_t
  {
    MQ_STATE_DISCONNECTED = 0,
    MQ_STATE_CONNECTING = 1,
    MQ_STATE_CONNECTED = 2,
    MQ_STATE_DISCONNECTING = 3,
    MQ_STATE_DONE = 4,
    MQ_STATE_ERROR = 4,
  };

  /*
   */
  enum class MqttReasonCode : uint8_t
  {
    MQ_RC_SUCCESS = 0,
    MQ_RC_UNSPECIFIED_ERROR = 0x80,
    MQ_RC_MALFORMED_PACKET = 0x81,
    MQ_RC_PROTOCOL_ERROR = 0x82,
    MQ_RC_IMPLEMENTATION_SPECIFIC_ERROR = 0x83,
    MQ_RC_UNSUPPORTED_PROTOCOL_VERSION = 0x84,
    MQ_RC_CLIENT_IDENTIFIER_NOT_VALID = 0x85,
    MQ_RC_BAD_USER_NAME_OR_PASSWORD = 0x86,
    MQ_RC_NOT_AUTHORIZED = 0x87,
    MQ_RC_SERVER_UNAVAILABLE = 0x88,
    MQ_RC_SERVER_BUSY = 0x89,
    MQ_RC_BANNED = 0x8A,
    MQ_RC_BAD_AUTHENTICATION_METHOD = 0x8C,
    MQ_RC_TOPIC_NAME_INVALID = 0x90,
    MQ_RC_PACKET_TOO_LARGE = 0x95,
    MQ_RC_QUOTA_EXCEEDED = 0x97,
    MQ_RC_PAYLOAD_FORMAT_INVALID = 0x99,
    MQ_RC_RETAIN_NOT_SUPPORTED = 0x9A,
    MQ_RC_QOS_NOT_SUPPORTED = 0x9B,
    MQ_RC_USE_ANOTHER_SERVER = 0x9C,
    MQ_RC_SERVER_MOVED = 0x9D,
    MQ_RC_CONNECTION_RATE_EXCEEDED = 0x9F
  };

  struct __attribute__((packed)) MqttConnect
  {
    uint8_t protocolName[6];
    uint8_t version;
    MqttConnectFlags flags;
    uint16_t keepAlive;
    uint8_t propertyLength;
  };

  // supports non blocking io
  template <typename LogTrait, typename StreamTrait, typename PublishTrait>
  class MqttManager
  {
  public:
    typedef std::function<int(int)> write_cb_type;

    MqttManager(LogTrait logger, write_cb_type set_write) : _logger(logger), _set_write(set_write), _nextPacketIdentifier(1)
    {
    }
    virtual ~MqttManager()
    {
    }
    MqttManager(const MqttManager &other) = delete;
    MqttManager &operator=(const MqttManager &other) = delete;
    MqttManager(MqttManager &&other) = delete;
    MqttManager &operator=(MqttManager &&other) = delete;

    int Register(int fd,
                 std::function<int(int, const struct iovec *, int)> readv,
                 std::function<int(int, const struct iovec *, int)> writev,
                 const std::shared_ptr<StreamTrait> &dataStream,
                 const std::shared_ptr<PublishTrait> &mqttOutStream)
    {
      auto sp = std::make_shared<con_type>(fd, readv, writev, dataStream, mqttOutStream);
      auto p = std::make_pair(fd, sp);
      assert(_connections.find(fd) == _connections.end());

      return _connections.insert(p).second;
    }

    int Unregister(int fd)
    {
      auto x = _connections.find(fd);
      if (x == _connections.end())
        return -1;
      std::shared_ptr<con_type> &con = (*x).second;

      _connections.erase(fd);
      return 0;
    }

    int Read(int fd)
    {
      auto x = _connections.find(fd);
      if (x == _connections.end())
        return -1;
      std::shared_ptr<con_type> &con = (*x).second;
      if (!con)
        return -2;

      int r = con->_dataStream->Readv(fd, con->_readv);
      if (r < 0)
        return -3;

      if (con->_dataStream->Available() >= (sizeof(MqttFixed) + 2))
      {
        MqttFixed fixed;
        char f;
        con->_dataStream->Peak(0, f);
        fixed = *reinterpret_cast<MqttFixed *>(&f);

        char a, b;
        uint16_t len = 0;

        // TODO: Cleanup
        // we have to peak to get the variable length
        // Peak at 0, 1, 2,  (assumge 2 bytes max length)
        con->_dataStream->Peak(1, a);
        int skip = 1;
        if (a & 0x80)
        {
          con->_dataStream->Peak(2, b);
          assert(!(b & 0x80));
          len = (a & 0x7F) << 8 | b;
          skip = 3;
        }
        else
        {
          len = a;
          skip = 2;
        }
        if (len > con->_dataStream->Available())
          return 0;

        con->_dataStream->Skip(skip); // skip what we peaked

        assert(len < 8192);
        char buf[8192];
        con->_dataStream->Pop(buf, len);

        // process data
        switch (fixed.control_packet_type)
        {
        case static_cast<uint8_t>(MqttControlPacketType::MQ_CPT_CONNECT):
          ECHIDNA_LOG_DEBUG(_logger, "MQTT CONNECT");
          break;
        case static_cast<uint8_t>(MqttControlPacketType::MQ_CPT_CONNACK):
          ECHIDNA_LOG_DEBUG(_logger, "MQTT CONNACK");
          log_reason_code(buf[1]);
          break;

        case static_cast<uint8_t>(MqttControlPacketType::MQ_CPT_PUBLISH):
        {
          bool retain = fixed.flags & static_cast<uint8_t>(MqttFlags::MQ_FLAG_PUBLISH_RETAIN);
          bool dup = fixed.flags & static_cast<uint8_t>(MqttFlags::MQ_FLAG_PUBLISH_DUP);
          bool atLeastonce = fixed.flags & static_cast<uint8_t>(MqttFlags::MQ_FLAG_PUBLISH_QOS2);
          bool once = fixed.flags & static_cast<uint8_t>(MqttFlags::MQ_FLAG_PUBLISH_QOS1);
          ECHIDNA_LOG_DEBUG(_logger, "MQTT PUBLISH retain[{}] dup[{}] atLeastonce[{}] once[{}] len[{}]",
                            retain ? "T" : "F",
                            dup ? "T" : "F",
                            atLeastonce ? "T" : "F",
                            once ? "T" : "F",
                            len);

          uint16_t topic_len = ntohs(*reinterpret_cast<uint16_t *>(buf));
          std::string topic(&buf[2], topic_len);
          ECHIDNA_LOG_DEBUG(_logger, "topic[{}]", topic);

          uint16_t packet_id = ntohs(*reinterpret_cast<uint16_t *>(&buf[2 + topic_len]));
          ECHIDNA_LOG_DEBUG(_logger, "packet_id[{}]", packet_id);

          // uint8_t property_len = buf[4 + topic_len];
          // ECHIDNA_LOG_DEBUG(_logger, "property_len[{}]", property_len);

          // uint16_t payload_len = len - (5 + topic_len + property_len);
          // std::string payload(&buf[5 + topic_len + property_len], payload_len);
          // ECHIDNA_LOG_DEBUG(_logger, "payload[{}]", payload);
        }
        break;
        case static_cast<uint8_t>(MqttControlPacketType::MQ_CPT_PUBACK):
          ECHIDNA_LOG_DEBUG(_logger, "MQTT PUBACK");
          break;
        case static_cast<uint8_t>(MqttControlPacketType::MQ_CPT_PUBREC):
          ECHIDNA_LOG_DEBUG(_logger, "MQTT PUBREC");
          break;
        case static_cast<uint8_t>(MqttControlPacketType::MQ_CPT_PUBREL):
          ECHIDNA_LOG_DEBUG(_logger, "MQTT PUBREL");
          break;
        case static_cast<uint8_t>(MqttControlPacketType::MQ_CPT_PUBCOMP):
          ECHIDNA_LOG_DEBUG(_logger, "MQTT PUBCOMP");
          break;
        case static_cast<uint8_t>(MqttControlPacketType::MQ_CPT_SUBSCRIBE):
          ECHIDNA_LOG_DEBUG(_logger, "MQTT SUBSCRIBE");
          break;
        case static_cast<uint8_t>(MqttControlPacketType::MQ_CPT_SUBACK):
          ECHIDNA_LOG_DEBUG(_logger, "MQTT SUBACK");
          break;
        case static_cast<uint8_t>(MqttControlPacketType::MQ_CPT_UNSUBSCRIBE):
          ECHIDNA_LOG_DEBUG(_logger, "MQTT UNSUBSCRIBE");
          break;
        case static_cast<uint8_t>(MqttControlPacketType::MQ_CPT_UNSUBACK):
          ECHIDNA_LOG_DEBUG(_logger, "MQTT UNSUBACK");
          break;
        case static_cast<uint8_t>(MqttControlPacketType::MQ_CPT_PINGREQ):
          ECHIDNA_LOG_DEBUG(_logger, "MQTT PINGREQ");
          break;
        case static_cast<uint8_t>(MqttControlPacketType::MQ_CPT_PINGRESP):
          ECHIDNA_LOG_DEBUG(_logger, "MQTT PINGRESP");
          break;
        case static_cast<uint8_t>(MqttControlPacketType::MQ_CPT_DISCONNECT):
          ECHIDNA_LOG_DEBUG(_logger, "MQTT DISCONNECT");
          break;
        case static_cast<uint8_t>(MqttControlPacketType::MQ_CPT_AUTH):
          ECHIDNA_LOG_DEBUG(_logger, "MQTT AUTH");
          break;
        default:
          ECHIDNA_LOG_DEBUG(_logger, "Unknown MQTT packet type");
          break;
        }
      }

      return 0;
    }

    int Write(int fd)
    {
      auto x = _connections.find(fd);
      if (x == _connections.end())
        return -1;
      std::shared_ptr<con_type> &con = (*x).second;
      if (!con)
        return -2;

      if (!con->_writeBuf->IsEmpty())
      {
        int ret = con->_writeBuf->Writev(fd, con->_writev);

        if (ret < 0)
          return ret; // error

        return con->_writeBuf->IsEmpty() ? 0 : 1;
      }
      else if (con->_mqttOutStream)
      {
        // could limit size of write
        int ret = con->_mqttOutStream->Writev(con->_mqttOutStream->Available(fd), fd, con->_writev);

        if (ret < 0)
        {
          return ret; // error
        }
        return con->_mqttOutStream->IsEmpty(fd) ? 0 : 1;
      }

      return 0;
    }

    // Queue data
    bool Queue(int fd, const char *data, uint64_t len)
    {
      auto x = _connections.find(fd);
      if (x == _connections.end())
        return false;

      std::shared_ptr<con_type> &con = (*x).second;

      if (con)
      {
        con->_writeBuf->Push(data, len);
        return true;
      }
      return false;
    }

    bool Ping(int fd)
    {
      auto x = _connections.find(fd);
      if (x == _connections.end())
        return false;

      std::shared_ptr<con_type> &con = (*x).second;

      if (con)
      {
        // send ping
        uint8_t fixed = (static_cast<uint8_t>(MqttControlPacketType::MQ_CPT_PINGREQ) << 4);

        con->_writeBuf->Push(reinterpret_cast<const char *>(&fixed), sizeof(fixed));
        con->_writeBuf->Push("\0", 1);

        _set_write(fd);
        ECHIDNA_LOG_DEBUG(_logger, "MQTT PINGREQ -->");
        return true;
      }

      return false;
    }

    bool Subscribe(int fd, std::string topic)
    {
      auto x = _connections.find(fd);
      if (x == _connections.end())
        return false;

      std::shared_ptr<con_type> &con = (*x).second;

      if (con)
      {
        // reserved 0010 required
        uint8_t fixed = (static_cast<uint8_t>(MqttControlPacketType::MQ_CPT_SUBSCRIBE) << 4) | 0x02;
        con->_writeBuf->Push(reinterpret_cast<const char *>(&fixed), sizeof(fixed));

        // variable header
        uint16_t packet_id = _nextPacketIdentifier++;
        con->_variableBuf->Push(reinterpret_cast<const char *>(&packet_id), sizeof(packet_id));
        con->_variableBuf->Push("\0", 1); // properties

        uint16_t topic_len = htons(topic.length());
        con->_variableBuf->Push(reinterpret_cast<const char *>(&topic_len), sizeof(topic_len));
        con->_variableBuf->Push(topic.c_str(), topic.length());

        MqttSubscribeOptions subOptions = MqttSubscribeOptions::MQ_SO_MAX_QOS0;
        con->_variableBuf->Push(reinterpret_cast<const char *>(&subOptions), sizeof(subOptions));

        encodeVarInt(con, con->_variableBuf->Available());
        con->_variableBuf->PopAll([con](const char *data, uint64_t len) -> bool
                                  {
          con->_writeBuf->Push(data, len);
          return true; }, con->_variableBuf->Available());

        _set_write(fd);
        return true;
      }

      return false;
    }

    bool Connect(int fd, uint16_t keepAlive)
    {
      auto x = _connections.find(fd);
      if (x == _connections.end())
        return false;

      std::shared_ptr<con_type> &con = (*x).second;

      if (con)
      {
        uint8_t fixed = (static_cast<uint8_t>(MqttControlPacketType::MQ_CPT_CONNECT) << 4);
        assert(Queue(fd, reinterpret_cast<const char *>(&fixed), sizeof(fixed)));

        MqttConnect connect;
        ::memset(&connect, 0, sizeof(MqttConnect));
        connect.protocolName[0] = 0;
        connect.protocolName[1] = 4;
        connect.protocolName[2] = 'M';
        connect.protocolName[3] = 'Q';
        connect.protocolName[4] = 'T';
        connect.protocolName[5] = 'T';
        connect.version = MQTT_PROTOCOL_VERSION;
        connect.flags = MqttConnectFlags::MQ_CF_USER_NAME_FLAG | MqttConnectFlags::MQ_CF_PASSWORD_FLAG | MqttConnectFlags::MQ_CF_CLEAN_SESSION;
        connect.keepAlive = keepAlive; // 60 seconds
        connect.propertyLength = 0;    // no properties

        encodeVarInt(con, sizeof(MqttConnect) + 14 + 6);

        assert(Queue(fd, reinterpret_cast<const char *>(&connect), sizeof(MqttConnect)));

        // Client Identifier, Will Properties, Will Topic, Will Payload, User Name, Password
        // TODO Check each flag, to see what we should sned,

        // client id (always sent)
        uint16_t client_id_len = htons(6);
        assert(Queue(fd, reinterpret_cast<const char *>(&client_id_len), sizeof(uint16_t)));
        assert(Queue(fd, "waldid", 6));

        // will send user
        client_id_len = htons(4);
        assert(Queue(fd, reinterpret_cast<const char *>(&client_id_len), sizeof(uint16_t)));
        assert(Queue(fd, "wald", 4));

        // will send password
        client_id_len = htons(4);
        assert(Queue(fd, reinterpret_cast<const char *>(&client_id_len), sizeof(uint16_t)));
        assert(Queue(fd, "wald", 4));
        _set_write(fd);
        return true;
      }

      return true;
    }

    bool Connect()
    {
      return true;
    }

  private:
    void log_reason_code(uint8_t code)
    {
      switch (code)
      {
      case static_cast<uint8_t>(MqttReasonCode::MQ_RC_SUCCESS):
        ECHIDNA_LOG_INFO(_logger, "MQ_RC_SUCCESS");
        break;
      case static_cast<uint8_t>(MqttReasonCode::MQ_RC_UNSPECIFIED_ERROR):
        ECHIDNA_LOG_INFO(_logger, "MQ_RC_UNSPECIFIED_ERROR");
        break;
      case static_cast<uint8_t>(MqttReasonCode::MQ_RC_MALFORMED_PACKET):
        ECHIDNA_LOG_INFO(_logger, "MQ_RC_MALFORMED_PACKET");
        break;
      case static_cast<uint8_t>(MqttReasonCode::MQ_RC_PROTOCOL_ERROR):
        ECHIDNA_LOG_INFO(_logger, "MQ_RC_PROTOCOL_ERROR");
        break;
      case static_cast<uint8_t>(MqttReasonCode::MQ_RC_IMPLEMENTATION_SPECIFIC_ERROR):
        ECHIDNA_LOG_INFO(_logger, "MQ_RC_IMPLEMENTATION_SPECIFIC_ERROR");
        break;
      case static_cast<uint8_t>(MqttReasonCode::MQ_RC_UNSUPPORTED_PROTOCOL_VERSION):
        ECHIDNA_LOG_INFO(_logger, "MQ_RC_UNSUPPORTED_PROTOCOL_VERSION");
        break;
      case static_cast<uint8_t>(MqttReasonCode::MQ_RC_CLIENT_IDENTIFIER_NOT_VALID):
        ECHIDNA_LOG_INFO(_logger, "MQ_RC_CLIENT_IDENTIFIER_NOT_VALID");
        break;
      case static_cast<uint8_t>(MqttReasonCode::MQ_RC_BAD_USER_NAME_OR_PASSWORD):
        ECHIDNA_LOG_INFO(_logger, "MQ_RC_BAD_USER_NAME_OR_PASSWORD");
        break;
      case static_cast<uint8_t>(MqttReasonCode::MQ_RC_NOT_AUTHORIZED):
        ECHIDNA_LOG_INFO(_logger, "MQ_RC_NOT_AUTHORIZED");
        break;
      case static_cast<uint8_t>(MqttReasonCode::MQ_RC_SERVER_UNAVAILABLE):
        ECHIDNA_LOG_INFO(_logger, "MQ_RC_SERVER_UNAVAILABLE");
        break;
      case static_cast<uint8_t>(MqttReasonCode::MQ_RC_SERVER_BUSY):
        ECHIDNA_LOG_INFO(_logger, "MQ_RC_SERVER_BUSY");
        break;
      case static_cast<uint8_t>(MqttReasonCode::MQ_RC_BANNED):
        ECHIDNA_LOG_INFO(_logger, "MQ_RC_BANNED");
        break;
      case static_cast<uint8_t>(MqttReasonCode::MQ_RC_BAD_AUTHENTICATION_METHOD):
        ECHIDNA_LOG_INFO(_logger, "MQ_RC_BAD_AUTHENTICATION_METHOD");
        break;
      case static_cast<uint8_t>(MqttReasonCode::MQ_RC_TOPIC_NAME_INVALID):
        ECHIDNA_LOG_INFO(_logger, "MQ_RC_TOPIC_NAME_INVALID");
        break;
      case static_cast<uint8_t>(MqttReasonCode::MQ_RC_PACKET_TOO_LARGE):
        ECHIDNA_LOG_INFO(_logger, "MQ_RC_PACKET_TOO_LARGE");
        break;
      case static_cast<uint8_t>(MqttReasonCode::MQ_RC_QUOTA_EXCEEDED):
        ECHIDNA_LOG_INFO(_logger, "MQ_RC_QUOTA_EXCEEDED");
        break;
      case static_cast<uint8_t>(MqttReasonCode::MQ_RC_PAYLOAD_FORMAT_INVALID):
        ECHIDNA_LOG_INFO(_logger, "MQ_RC_PAYLOAD_FORMAT_INVALID");
        break;
      case static_cast<uint8_t>(MqttReasonCode::MQ_RC_RETAIN_NOT_SUPPORTED):
        ECHIDNA_LOG_INFO(_logger, "MQ_RC_RETAIN_NOT_SUPPORTED");
        break;
      case static_cast<uint8_t>(MqttReasonCode::MQ_RC_QOS_NOT_SUPPORTED):
        ECHIDNA_LOG_INFO(_logger, "MQ_RC_QOS_NOT_SUPPORTED");
        break;
      case static_cast<uint8_t>(MqttReasonCode::MQ_RC_USE_ANOTHER_SERVER):
        ECHIDNA_LOG_INFO(_logger, "MQ_RC_USE_ANOTHER_SERVER");
        break;
      case static_cast<uint8_t>(MqttReasonCode::MQ_RC_SERVER_MOVED):
        ECHIDNA_LOG_INFO(_logger, "MQ_RC_SERVER_MOVED");
        break;
      case static_cast<uint8_t>(MqttReasonCode::MQ_RC_CONNECTION_RATE_EXCEEDED):
        ECHIDNA_LOG_INFO(_logger, "MQ_RC_CONNECTION_RATE_EXCEEDED");
        break;
      default:
        ECHIDNA_LOG_INFO(_logger, "Unknown reason code");
        break;
      }
    }

    typedef struct MqttConnection
    {
      std::shared_ptr<coypu::buf::BipBuf<char, uint64_t>> _writeBuf;
      std::shared_ptr<coypu::buf::BipBuf<char, uint64_t>> _variableBuf;

      MqttConnection(int fd,
                     std::function<int(int, const struct iovec *, int)> readv,
                     std::function<int(int, const struct iovec *, int)> writev,
                     const std::shared_ptr<StreamTrait> &dataStream,
                     const std::shared_ptr<PublishTrait> &mqttOutStream) : _fd(fd), _readv(readv), _writev(writev), _dataStream(dataStream), _mqttOutStream(mqttOutStream)
      {
        uint64_t capacity = 8192;
        _writeData = new char[capacity];
        _variableData = new char[capacity];

        // data to send to the socket
        _writeBuf = std::make_shared<coypu::buf::BipBuf<char, uint64_t>>(_writeData, capacity);

        // use this for variable length data
        // mqtt requires this double copy because of the variable length nature
        _variableBuf = std::make_shared<coypu::buf::BipBuf<char, uint64_t>>(_variableData, capacity);
      }

      ~MqttConnection()
      {
        if (_writeData)
          delete[] _writeData;

        if (_variableData)
          delete[] _variableData;
      }

      int _fd;
      std::function<int(int, const struct iovec *, int)> _readv;
      std::function<int(int, const struct iovec *, int)> _writev;
      std::shared_ptr<StreamTrait> _dataStream;
      std::shared_ptr<PublishTrait> _mqttOutStream;
      char *_writeData;
      char *_variableData;
    } con_type;

    void encodeVarInt(std::shared_ptr<con_type> &con, uint8_t remainingLength)
    {
      do
      {
        uint8_t encodedByte = remainingLength % 128;
        remainingLength /= 128;
        if (remainingLength > 0)
        {
          encodedByte |= 128;
        }

        con->_writeBuf->Push(reinterpret_cast<const char *>(&encodedByte), sizeof(uint8_t));
      } while (remainingLength > 0);
    }

    std::unordered_map<int, std::shared_ptr<con_type>> _connections;
    LogTrait _logger;
    write_cb_type _set_write;
    uint16_t _nextPacketIdentifier;
  };
} // namespace eight99bushwick
