// The MIT License (MIT)
//
// Copyright (c) INTERNET OF THINGER SL
// Author: alvarolb@gmail.com (Alvaro Luis Bustamante)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef THINGER_IOTMP_MACROS_HPP
#define THINGER_IOTMP_MACROS_HPP

// Platform-independent macros
#ifndef outputValue
#define outputValue(value) [](thinger::iotmp::output& out){ out = value; }
#endif

#ifndef outputString
#define outputString(value) [](thinger::iotmp::output& out){ out = value; }
#endif

// Platform-specific macros — no-ops by default, override in platform headers
#ifndef digitalPin
#define digitalPin(PIN) [](thinger::iotmp::input& in) { \
    static bool state = false;                           \
    if(in.is_empty()) { in = state; }                    \
    (void)PIN;                                           \
}
#endif

#ifndef analogPin
#define analogPin(PIN) [](thinger::iotmp::output& out){ (void)out; (void)PIN; }
#endif

#endif // THINGER_IOTMP_MACROS_HPP
