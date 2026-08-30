#pragma once

// ALSA device enumeration.
//
// Uses libasound directly rather than shelling out to `aplay -l`. Parsing another
// program's human-readable output is the pattern this port exists to get away
// from, and it breaks silently when that output changes.
//
// On a machine with no sound card this correctly returns an EMPTY list. That is
// the honest answer, and a client showing "no devices" is right; inventing a
// "default" entry would offer the operator a device that cannot transmit.

#include <string>
#include <vector>

struct AudioDevice {
  int index = 0;
  std::string name;
};

// Playback-capable cards, in the reference host's display format:
//   "card 0: CODEC [USB AUDIO  CODEC], device 0: USB Audio [USB Audio]"
std::vector<AudioDevice> ListPlaybackDevices();
