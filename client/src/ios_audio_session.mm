// The iOS audio session — the half of "background audio" that is not a plist key.
//
// ⚠️ UIBackgroundModes=audio DOES NOT KEEP AUDIO PLAYING. It only says the app is
// ALLOWED to. What decides is the AVAudioSession category, and Qt's default on
// iOS is an ambient one: it is silenced by the ring/silent switch and stops dead
// the moment the app leaves the foreground. That is exactly the reported
// symptom - the panel keeps its socket, the host keeps sending, and the operator
// hears nothing until they bring the app back.
//
// PlayAndRecord is the category that survives backgrounding AND can open the
// microphone, which this app needs for transmit. Playback alone would go silent
// the first time the operator keys up.
//
// ⚠️ MODE IS LEFT AT DEFAULT ON PURPOSE. AVAudioSessionModeVoiceChat sounds like
// the right pick for a radio and is not: it turns on Apple's echo cancellation
// and automatic gain, which process the operator's voice before it reaches the
// transmitter. The rig's ALC is what sets transmit level here (WIP.md 8g), and a
// second automatic gain fighting it is how a signal ends up splattering.
//
// ⚠️ AND AN INTERRUPTION MUST BE ANSWERED. A phone call deactivates the session;
// without the notification below audio never comes back and the app looks dead
// while every counter reads healthy - the same failure class as everything else
// in this file's history.

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

#include <QString>

namespace {

QString g_state = QStringLiteral("audio session not configured");

QString Describe(AVAudioSession* s, bool active_ok, NSError* err) {
    // ⚠️ TRIMMED, because the footer has one line and iOS returns
    // "AVAudioSessionCategoryPlayAndRecord" - 36 characters of prefix that push
    // the part that matters off the end of the screen. section('.') was wrong:
    // the constant has no dots in it.
    NSString* cat = s.category ?: @"(none)";
    QString cat_s = QString::fromNSString(cat);
    cat_s.remove("AVAudioSessionCategory");
    QString out = QString("%1 %2 %3 Hz")
                      .arg(cat_s)
                      .arg(active_ok ? "active" : "INACTIVE")
                      .arg(static_cast<int>(s.sampleRate));
    if (!active_ok && err) {
        out += " - " + QString::fromNSString(err.localizedDescription);
    }
    return out;
}

}  // namespace

namespace ios_audio {

// Called once at startup and again whenever the app returns to the foreground.
// Idempotent: setting the same category twice is not an error, and re-activating
// an already-active session is how you recover from an interruption you missed.
QString Configure() {
    AVAudioSession* s = [AVAudioSession sharedInstance];
    NSError* err = nil;

    // DefaultToSpeaker: without it PlayAndRecord routes to the EARPIECE, and the
    // operator holds a radio panel to their ear to hear the band.
    // AllowBluetooth: a headset is the normal way to work a phone hands-free.
    const AVAudioSessionCategoryOptions opts =
        AVAudioSessionCategoryOptionDefaultToSpeaker |
        AVAudioSessionCategoryOptionAllowBluetooth |
        AVAudioSessionCategoryOptionAllowBluetoothA2DP;

    if (![s setCategory:AVAudioSessionCategoryPlayAndRecord
                   mode:AVAudioSessionModeDefault
                options:opts
                  error:&err]) {
        g_state = QString("category REFUSED: %1")
                      .arg(QString::fromNSString(err.localizedDescription));
        return g_state;
    }

    NSError* aerr = nil;
    const bool active = [s setActive:YES error:&aerr];
    g_state = Describe(s, active, aerr);

    static bool observing = false;
    if (!observing) {
        observing = true;
        [[NSNotificationCenter defaultCenter]
            addObserverForName:AVAudioSessionInterruptionNotification
                        object:s
                         queue:[NSOperationQueue mainQueue]
                    usingBlock:^(NSNotification* note) {
                      const NSUInteger type =
                          [note.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
                      if (type == AVAudioSessionInterruptionTypeEnded) {
                          // ⚠️ Re-activate, do not assume. The session is dead
                          // until something says otherwise.
                          NSError* rerr = nil;
                          const bool ok = [[AVAudioSession sharedInstance] setActive:YES
                                                                              error:&rerr];
                          g_state = Describe([AVAudioSession sharedInstance], ok, rerr);
                      } else {
                          g_state = QStringLiteral("interrupted");
                      }
                    }];
    }
    return g_state;
}

QString State() { return g_state; }

}  // namespace ios_audio
