// Linux stand-in for <dxerr.h>. In release builds the SDK defines DXTRACE_ERR(str, hr) as just (hr);
// the message argument is not even evaluated. Same here.
#pragma once
#define DXTRACE_ERR(str, hr) (hr)
