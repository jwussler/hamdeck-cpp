#pragma once

// The station admin console, served as one page by the host itself.
//
// ⚠️ IT IS EMBEDDED IN THE BINARY, NOT READ FROM DISK, and the host sets no
// civetweb document_root. A document_root defaults to the WORKING DIRECTORY, so
// enabling static file serving to ship one page would quietly expose whatever
// the service happens to be running in. One string constant has no such edge.
//
// ⚠️ THE PAGE IS ANONYMOUS; EVERYTHING IT CALLS IS NOT. It is markup and script
// with no station data in it, and it has to render before there is a session or
// there is nowhere to type a password. Every button behind it goes through
// /api/admin/*, which the admin gate has already covered.
extern const char* const kAdminPageHtml;
