
#pragma once

#include <cstdint>
#include <functional>

namespace eight99bushwick
{
  namespace piapiac
  {

    struct MqttFixed
    {
      uint8_t control_packet_type : 4;
      uint8_t flags : 4;
    };
    // followed by variable length
    // followed by packet identifier (sometimes)

    enum class MqttControlPacketType : uint8_t
    {
      RESERVED = 0,
      CONNECT = 1,
      CONNACK = 2,
      PUBLISH = 3,
      PUBACK = 4,
      PUBREC = 5,
      PUBREL = 6,
      PUBCOMP = 7,
      SUBSCRIBE = 8,
      SUBACK = 9,
      UNSUBSCRIBE = 10,
      UNSUBACK = 11,
      PINGREQ = 12,
      PINGRESP = 13,
      DISCONNECT = 14,
      AUTH = 15
    };

    enum class MqttFlags : uint8_t
    {
      PUBLISH_DUP = 0x08,
      PUBLISH_QOS2 = 0x04,
      PUBLISH_QOS1 = 0x02,
      PUBLISH_RETAIN = 0x01,
    };

    enum class MqttState : uint8_t
    {
      DISCONNECTED = 0,
      CONNECTING = 1,
      CONNECTED = 2,
      DISCONNECTING = 3,
      DONE = 4,
      ERROR = 4,
    };

    // supports non blocking io
    class MqttManager
    {
    public:
      MqttManager();
      virtual ~MqttManager();
      MqttManager(const MqttManager &other) = delete;
      MqttManager &operator=(const MqttManager &other) = delete;
      MqttManager(MqttManager &&other) = delete;
      MqttManager &operator=(MqttManager &&other) = delete;

      int Register(int fd, const std::function<int(int)> &readCB,
                   const std::function<int(int)> &writeCB,
                   const std::function<int(int)> &closeCB);
    };
  }
}