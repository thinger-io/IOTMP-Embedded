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

#ifndef THINGER_IOTMP_LOG_HPP
#define THINGER_IOTMP_LOG_HPP

// Logging macros for IOTMP protocol.
// By default these are no-ops. Each platform defines them before
// including the core headers:
//
// Arduino: maps to Serial.printf() when THINGER_SERIAL_DEBUG is defined
// Zephyr:  maps to Zephyr LOG_MODULE (LOG_INF, LOG_DBG, LOG_ERR, LOG_WRN)
// Other:   define THINGER_LOG_INFO etc. before including iotmp.hpp

#ifndef THINGER_LOG_ERROR
#define THINGER_LOG_ERROR(fmt, ...)
#endif

#ifndef THINGER_LOG_WARNING
#define THINGER_LOG_WARNING(fmt, ...)
#endif

#ifndef THINGER_LOG_INFO
#define THINGER_LOG_INFO(fmt, ...)
#endif

#ifndef THINGER_LOG_DEBUG
#define THINGER_LOG_DEBUG(fmt, ...)
#endif

#endif
