
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <sys/uio.h>
#include <assert.h>
#include <echidna/buf.hpp>

namespace eight99bushwick::piapiac
{
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

  enum class MqttState : uint8_t
  {
    MQ_STATE_DISCONNECTED = 0,
    MQ_STATE_CONNECTING = 1,
    MQ_STATE_CONNECTED = 2,
    MQ_STATE_DISCONNECTING = 3,
    MQ_STATE_DONE = 4,
    MQ_STATE_ERROR = 4,
  };

  struct __attribute__((packed)) MqttConnect  {
    uint8_t protocolName[6];
    uint8_t version;
    uint8_t flags;
    uint16_t keepAlive;
    uint8_t propertyLength;
  };

  // supports non blocking io
  template <typename StreamTrait, typename PublishTrait>
  class MqttManager
  {
  public:
      typedef std::function<int(int)> write_cb_type;

    MqttManager(write_cb_type set_write) : _set_write(set_write)
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

      if (con) {
        con->_writeBuf->Push(data, len);
        _set_write(fd);
        return true;
      }
      return false;
    }

    bool Connect (int fd) {
      auto x = _connections.find(fd);
      if (x == _connections.end())
        return false;

      std::shared_ptr<con_type> &con = (*x).second;

      if (con) {
        // send connect
        MqttFixed fixed;
        fixed.control_packet_type = static_cast<uint8_t>(MqttControlPacketType::MQ_CPT_CONNECT);
        fixed.flags = 0;
        assert(Queue(fd, reinterpret_cast<const char *>(&fixed), sizeof(MqttFixed)));
        // Remaining Length field

        MqttConnect connect;
        ::memset(&connect, 0, sizeof(MqttConnect));
        connect.protocolName[0] = 0;
        connect.protocolName[1] = 4;
        connect.protocolName[2] = 'M';
        connect.protocolName[3] = 'Q';
        connect.protocolName[4] = 'T';
        connect.protocolName[5] = 'T';
        connect.version = 5;
        connect.flags = 0x2; // clean session
        connect.keepAlive = 60; // 60 seconds
        connect.propertyLength = 0; // no properties

        // encode variable length
        encodeVarInt(fd, sizeof(MqttConnect));

        assert(Queue(fd, reinterpret_cast<const char *>(&connect), sizeof(MqttConnect)));

        return true;
      }

      return true;
    }

    bool Connect()
    {
      return true;
    }


  private:
    void encodeVarInt(int fd, uint8_t remainingLength)
    {
      do
      {
        uint8_t encodedByte = remainingLength % 128;
        remainingLength /= 128;
        if (remainingLength > 0)
        {
          encodedByte |= 128;
        }
        Queue(fd, reinterpret_cast<const char *>(&encodedByte), sizeof(uint8_t));
      } while (remainingLength > 0);
    }

    typedef struct MqttConnection
    {
      std::shared_ptr<coypu::buf::BipBuf<char, uint64_t>> _writeBuf;

      MqttConnection(int fd,
                     std::function<int(int, const struct iovec *, int)> readv,
                     std::function<int(int, const struct iovec *, int)> writev,
                     const std::shared_ptr<StreamTrait> &dataStream,
                     const std::shared_ptr<PublishTrait> &mqttOutStream) : _fd(fd), _readv(readv), _writev(writev), _dataStream(dataStream), _mqttOutStream(mqttOutStream)
      {
        uint64_t capacity = 8192;
        _writeData = new char[capacity];
        _writeBuf = std::make_shared<coypu::buf::BipBuf<char, uint64_t>>(_writeData, capacity);
      }

      ~MqttConnection()
      {
        if (_writeData)
          delete[] _writeData;
      }

      int _fd;
      std::function<int(int, const struct iovec *, int)> _readv;
      std::function<int(int, const struct iovec *, int)> _writev;
      std::shared_ptr<StreamTrait> _dataStream;
      std::shared_ptr<PublishTrait> _mqttOutStream;
      char *_writeData;
    } con_type;

    std::unordered_map<int, std::shared_ptr<con_type>> _connections;
    write_cb_type _set_write;
  };
} // namespace eight99bushwick
