#pragma once

// Reported by /api/health as "service" and "version".
//
// The service string MUST stay distinguishable from the C# host's
// "HamDeck API (C#)". Both hosts can be reachable at once (the VM and the VM),
// and when a client misbehaves the first question is always which one answered.
inline constexpr const char* kServiceName = "HamDeck API (C++)";
inline constexpr const char* kVersion     = "0.1.0";
