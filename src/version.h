#pragma once

// Reported by /api/health as "service" and "version".
//
// The service string MUST stay distinguishable from the C# host's
// "HamDeck API (C#)". Both hosts can be reachable at once (the reference host and the C++ host),
// and when a client misbehaves the first question is always which one answered.
inline constexpr const char* kServiceName = "HamDeck API (C++)";
// ⚠️ FROM THE BUILD, NOT A LITERAL. This said "0.1.0" while the clients shipped
// 0.1.33, so /api/health and /api/build reported a number that told an operator
// nothing about what their station was running - and the version is the first
// thing anyone asks for when something is wrong. HAMDECK_VERSION is defined by
// CMake from the git tag; the fallback only applies to a build that was given no
// version at all, and says so rather than inventing a plausible one.
#ifdef HAMDECK_VERSION
inline constexpr const char* kVersion     = HAMDECK_VERSION;
#else
inline constexpr const char* kVersion     = "0.0.0-untagged";
#endif
