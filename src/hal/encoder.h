#pragma once

// The rotary encoder, on the MCP23017 I/O expander (0x20 on the shared I2C
// bus): S1 and S2 on GPA4 and GPA5, its button (S3) on GPA6, pulled up on the
// encoder's own board and by the expander too (so an unplugged encoder reads
// as at rest). A task of its own reads them every millisecond; the UI takes
// what happened since. Without the expander nothing runs. Wire must already
// be started.
namespace encoder {

bool begin();  // false, and nothing runs, without the expander

int takeSteps();       // clicks turned since the last call: + clockwise
bool takePress();      // a short press, once it's let go
bool takeLongPress();  // held a while (the moment it's long enough)

void setLogging(bool on);  // dev console: each click and press on Serial
bool logging();

}  // namespace encoder
