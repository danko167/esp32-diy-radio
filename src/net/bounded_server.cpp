#include "bounded_server.h"

#include <algorithm>
#include <new>

namespace {

constexpr size_t kMaxHead = 8 * 1024;  // the request line and the headers
constexpr size_t kMaxLine = 2 * 1024;  // any one of them
// For the head, and the body unless it's raw: sent a byte every few seconds,
// one would otherwise hold the server for hours
constexpr uint32_t kMaxWaitMs = 10000;

// Why a request was cut short, and its answer
struct Refusal {
    int status;
    const char *text;
};
constexpr Refusal kTooBig{413, "Too big"};
constexpr Refusal kTooSlow{408, "Too slow"};
constexpr Refusal kBadLength{400, "Bad Content-Length"};
constexpr Refusal kStopping{503, "The upload page is stopping"};
constexpr Refusal kNoMemory{503, "Out of memory"};

}  // namespace

// The request's connection, read through the budget; a copy shares the
// socket and its buffer. Spent, it throws: WebServer's form parser would
// otherwise wait forever for a boundary that never comes. Its parser holds
// what it reads in Strings, which unwinding frees; the one raw buffer, for a
// plain body, gets at most the budget's available(), so it's never thrown past.
// The head and the body have budgets of their own: the head's ends at its
// blank line, so a long header can't spend what the body was meant to have.
class BoundedServer::Budget : public NetworkClient {
  public:
    Budget(BoundedServer &server, const NetworkClient &client)
        : NetworkClient(client), server(server), left(kMaxHead), started(millis()) {}

    using NetworkClient::read;

    int available() override {
        if (raw()) return cut() ? 0 : NetworkClient::available();
        checkLength();
        // Not thrown: it would be past that plain body buffer. It gets
        // nothing more instead, so the parser gives up
        if (const Refusal *r = late()) {
            refusal = *r;
            return 0;
        }
        if (!left) refusal = kTooBig;
        return std::min<size_t>(NetworkClient::available(), left);
    }

    int read(uint8_t *buf, size_t size) override {
        if (raw()) return cut() ? -1 : NetworkClient::read(buf, size);
        checkLength();
        if (const Refusal *r = late()) throw *r;
        if (!left) throw kTooBig;
        const int n = NetworkClient::read(buf, std::min(size, left));
        for (int i = 0; i < n; i++) take(buf[i]);
        return n;
    }

    // Why the parser wanted more than the budget gave, if it did (status 0 if not)
    const Refusal &refused() const { return refusal; }

  private:
    // A raw body streams to its handler, past the budget
    bool raw() const { return server._currentRaw != nullptr; }

    // A raw body ends with the server cancelled, or its connection closed by
    // its handler: this copy's buffer would still read the closed socket's
    // number, by then maybe another's
    bool cut() const { return server.cancelled || fd() < 0; }

    // The server cancelled, or the head (or a plain body) taking too long
    const Refusal *late() const {
        if (server.cancelled) return &kStopping;
        if (millis() - started >= kMaxWaitMs) return &kTooSlow;
        return nullptr;
    }

    // Known once its header is read: too long, it's refused before the body
    void checkLength() {
        if (server._clientContentLength < 0) throw kBadLength;  // a raw body would be read for good
        if ((size_t)server._clientContentLength <= server.maxBody) return;
        RequestHandler *h = server._currentHandler;
        if (!h || !h->canRaw(server, server._currentUri)) throw kTooBig;
    }

    // One byte read: in the head, lines are counted, and its blank line
    // hands over to the body's budget (WebServer reads the head a byte at a
    // time, so nothing of the body is counted against the head)
    void take(uint8_t c) {
        left--;
        if (!inHead) return;
        if (c == '\n') {
            if (!line) {
                inHead = false;
                left = server.maxBody;
            }
            line = 0;
        } else if (c != '\r' && ++line > kMaxLine) {
            throw kTooBig;
        }
    }

    BoundedServer &server;
    size_t left;
    const uint32_t started;
    bool inHead = true;
    size_t line = 0;  // characters in the head's current line
    Refusal refusal{};
};

void BoundedServer::handleClient() {
    if (_currentStatus == HC_NONE) {
        _currentClient = _server.accept();
        if (!_currentClient) {
            if (_nullDelay) delay(1);
            return;
        }
        _currentStatus = HC_WAIT_READ;
        _statusChange = millis();
    }

    bool keep = false;
    if (_currentClient.connected() && _currentStatus == HC_WAIT_READ) {
        if (_currentClient.available()) {
            handling = true;
            _currentClient.setTimeout(HTTP_MAX_SEND_WAIT);
            // The budget checks these from the first byte on, but WebServer
            // resets them only after the request line: left from an oversized
            // request, they'd refuse every one after it
            _clientContentLength = 0;
            _currentHandler = nullptr;
            // Before the request is read, not after: a raw upload's handler
            // may answer while it is (refusing it at its start)
            _contentLength = CONTENT_LENGTH_NOT_SET;
            _responseCode = 0;
            _clearResponseHeaders();
            Budget client(*this, _currentClient);
            Refusal refusal{};
            try {
                const bool parsed = _parseRequest(client);
                refusal = client.refused();
                if (parsed && !refusal.status) _handleRequest();
            } catch (const Refusal &r) {
                refusal = r;
            } catch (const std::bad_alloc &) {
                // A body of very many arguments, say: refused, rather than the radio's end
                _currentArgCount = 0;  // their array may be gone
                refusal = kNoMemory;
            }
            // Unless it's been answered already
            if (refusal.status && !_responseCode) send(refusal.status, "text/plain", refusal.text);
            handling = false;
        } else if (millis() - _statusChange <= HTTP_MAX_DATA_WAIT) {
            keep = true;
        }
    }

    // One request per connection, as WebServer does
    if (!keep) {
        _currentClient = NetworkClient();
        _currentStatus = HC_NONE;
        _currentUpload.reset();
        _currentRaw.reset();
    }
}
