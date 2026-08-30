#include "alsa_devices.h"

#include <alsa/asoundlib.h>

#include <format>

std::vector<AudioDevice> ListPlaybackDevices() {
  std::vector<AudioDevice> out;
  int card = -1;
  int index = 0;

  while (snd_card_next(&card) >= 0 && card >= 0) {
    snd_ctl_t* ctl = nullptr;
    const std::string hw = std::format("hw:{}", card);
    if (snd_ctl_open(&ctl, hw.c_str(), 0) < 0) continue;

    snd_ctl_card_info_t* info = nullptr;
    snd_ctl_card_info_alloca(&info);
    if (snd_ctl_card_info(ctl, info) < 0) {
      snd_ctl_close(ctl);
      continue;
    }
    const std::string card_id = snd_ctl_card_info_get_id(info);
    const std::string card_name = snd_ctl_card_info_get_name(info);

    int device = -1;
    while (snd_ctl_pcm_next_device(ctl, &device) >= 0 && device >= 0) {
      snd_pcm_info_t* pcm = nullptr;
      snd_pcm_info_alloca(&pcm);
      snd_pcm_info_set_device(pcm, device);
      snd_pcm_info_set_subdevice(pcm, 0);
      snd_pcm_info_set_stream(pcm, SND_PCM_STREAM_PLAYBACK);
      if (snd_ctl_pcm_info(ctl, pcm) < 0) continue;   // no playback on this device

      out.push_back({index++,
                     std::format("card {}: {} [{}], device {}: {} [{}]", card, card_id,
                                 card_name, device, snd_pcm_info_get_name(pcm),
                                 snd_pcm_info_get_name(pcm))});
    }
    snd_ctl_close(ctl);
  }
  return out;
}
