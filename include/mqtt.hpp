
#pragma once

#include <cstdint>

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
  }
}