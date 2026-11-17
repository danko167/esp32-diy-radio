#pragma once

#include <WebServer.h>

// Arduino's WebServer reads a request's lines, and the whole body of all
// but raw uploads (form posts, JSON, multipart forms), into memory before
// any handler runs: any device on the Wi-Fi could send megabytes without
// the code. This one reads each request through a budget instead. A request
// over it, or with a Content-Length over maxBody that isn't a raw upload,
// gets a 413 and the connection is closed, before its body is read; one
// whose head (and body, unless raw) takes over 10 s gets a 408. Raw uploads
// (handlers with canRaw()) stream through their handlers, which check their
// sizes, and answer and close the connection on what they refuse.
//
// handleClient() is WebServer's own (Arduino core 3.3), less what this
// server doesn't use: middleware and server-sent events.
class BoundedServer : public WebServer {
  public:
    BoundedServer(int port, size_t maxBody) : WebServer(port), maxBody(maxBody) {}
    void handleClient() override;

    // From a raw body's start till its request is answered. A multipart
    // form's file reaches the same handler (as upload()), without raw().
    bool rawRequest() const { return _currentRaw != nullptr; }
    // A request is being read or answered, however long that takes
    bool busy() const { return handling; }
    // From any task: the request being read ends soon, and any after it is refused
    void cancel() { cancelled = true; }

  private:
    class Budget;
    const size_t maxBody;
    volatile bool handling = false;
    volatile bool cancelled = false;
};
