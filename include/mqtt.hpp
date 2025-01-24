
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