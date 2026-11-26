#pragma once

#include <cstdint>

inline bool wireBeginResult = true;
inline int wireSda = -1;
inline int wireScl = -1;
inline uint16_t wireTimeout = 0;

class TwoWire {
public:
    bool begin(int sda, int scl) {
        wireSda = sda;
        wireScl = scl;
        return wireBeginResult;
    }
    void setTimeOut(uint16_t timeout) { wireTimeout = timeout; }
};
inline TwoWire Wire;
