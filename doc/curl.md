# cURL bindings for Lua

## Synopsis

    require 'curl'
    c = curl.new()
    c:setopt(curl.OPT_URL, "http://example.com/path")
    c:perform()

## Functions

### curl.escape(string)

Escapes URL strings, suitable for use when building URLs.

    print(curl.escape("abcd$%^&*()"))
    abcd%24%25%5E%26%2A%28%29

### curl.unescape(string)

Unescapes URL encoding in strings, suitable for use when decomposing URLs.

    print(curl.unescape("abcd%24%25%5E%26%2A%28%29"));
    abcd$%^&*()

### curl.new()

Creates a new curl object.  The curl object serves as the main point of context
for managing a session.  You must create an object to be able to use any of the
network facing operations of the cURL library.

    c = curl.new()

#### Methods

#### c:perform()

Call this method to perform a file transfer after all setopt calls are made.

    c:perform()

#### c:close()

This function can be used to force closed all resources associated with
the curl object.  This is called automatically when the curl object is
garbage collected, so you will not typically need to call this function.

After calling the close function, the curl object cannot be used and
will result in a runtime error if you attempt to use it.

    c:close() -- equivalent to: c = nil; pick whichever suits you


##### c:setopt(option, value)

Set option value to a curl object c. The first parameter option is a number
representing the option which can be on of the following listed below. A
predefined constant curl.OPT\_XXXX corresponds to CURLOPT\_XXXX constant defined
in the libcurl interface curl/curl.h The accepted value type depends of the
corresponding option. Below are listed all curl options supported by libCURL
grouped by value type. For a complete documentation on the options below read
the curl manual.

#### Callback options

The following options are associated with callback functions.
The general paradigm is that there is a FUNCTION option to specify a lua
callback or closure, and an associated DATA option that can be used to
specify the first parameter passed to the FUNCTION.

##### curl.OPT\_READFUNCTION and curl.OPT\_READDATA

Sets the READFUNCTION option, which is used to override the source of data
used in an upload operation.  The callback must have the following signature:

    function(userdata, size)
      -- return a string of no more than "size" bytes, or nil when
      -- EOF is reached 
    end

When OPT\_READFUNCTION is set, it implicitly sets OPT\_READDATA to the curl
object.  You may set this separately; the value of OPT\_READDATA is passed
as the first parameter to the READFUNCTION.

##### curl.OPT\_WRITEFUNCTION and curl.OPT\_WRITEDATA

Sets the WRITEFUNCTION option, which is used to override the destination
of data received from download operation.  The callback must have the
following signature:

    function(userdata, datastring)
      -- datastring is the incoming data to store
      -- return an integer indicating how many bytes were stored
    end

When OPT\_WRITEFUNCTION is set, it implicitly sets OPT\_WRITEDATA to the curl
object.  You may set this separately; the value of OPT\_WRITEDATA is passed
as the first parameter to the WRITEFUNCTION.

##### curl.OPT\_HEADERFUNCTION and curl.OPT\_HEADERDATA

Sets the HEADERFUNCTION option, which is used to override the
destination of header or meta data received from download operation.
The callback must have the following signature:

    function(userdata, datastring)
      -- datastring is the incoming data to store
      -- return an integer indicating how many bytes were stored
    end

When OPT\_HEADERFUNCTION is set, it implicitly sets OPT\_HEADERDATA to the curl
object.  You may set this separately; the value of OPT\_HEADERDATA is passed
as the first parameter to the HEADERFUNCTION.

##### curl.OPT\_PROGRESSFUNCTION and curl.OPT\_PROGRESSDATA

Sets the PROGRESSFUNCTION option, which is used to override the
destination of header or meta data received from download operation.
Note that curl.OPT\_NOPROGRESS must be set to false for the
PROGRESSFUNCTION to be called.
The callback must have the following signature:

    function(userparam, dltotal, dlnow, uptotal, upnow)
      -- return 0 (or omit a return statement) to allow the operation
      -- to continue. Returning any other integer value will cause
      -- the operation to abort
    end

When OPT\_PROGRESSFUNCTION is set, it implicitly sets OPT\_PROGRESSDATA
to the curl object.  You may set this separately; the value of
OPT\_PROGRESSDATA is passed as the first parameter to the
PROGRESSFUNCTION.

##### curl.OPT\_IOCTLFUNCTION and curl.OPT\_IOCTLDATA

Sets the IOCTLFUNCTION option, which is used to override some aspects
of the cURL library operation--consult the cURL documentation to find
out more about this.
The callback must have the following signature:

    function(userparam, command)
      -- command is a numeric code
      -- return a numeric code dependent upon the result of the ioctl
      -- operation.  Consult the cURL documentation for more
      -- information.
    end

When OPT\_IOCTLFUNCTION is set, it implicitly sets OPT\_IOCTLDATA
to the curl object.  You may set this separately; the value of
OPT\_IOCTLDATA is passed as the first parameter to the
IOCTLFUNCTION.

##### curl.OPT\_SEEKFUNCTION and curl.OPT\_SEEKDATA

Sets the SEEKFUNCTION option, which is used to seek around in the input
stream for resumed uploads.  The callback must have the following signature:

    function(userparam, offset, origin)
      -- Return 0 on success, 1 on failure or 2 to indicate that seeking
      -- is incompatible and that cURL should workaround it by reading
      -- and discarding the data
    end

When OPT\_SEEKFUNCTION is set, it implicitly sets OPT\_SEEKDATA
to the curl object.  You may set this separately; the value of
OPT\_SEEKDATA is passed as the first parameter to the
SEEKFUNCTION.

#### String list options

The following options can be set to a string or a list of strings, or a
nil value to clear a previously configured list of strings.

    c:setopt(curl.OPT_HTTPHEADER, "X-Foo: Bar",
        "Content-Type: application/json");
    c:setopt(curl.OPT_HTTPHEADER, nil);

 * curl.OPT\_HTTP200ALIASES
 * curl.OPT\_HTTPHEADER
 * curl.OPT\_HTTPPOST
 * curl.OPT\_POSTQUOTE
 * curl.OPT\_PREQUOTE
 * curl.OPT\_QUOTE
 * curl.OPT\_SOURCE\_POSTQUOTE
 * curl.OPT\_SOURCE\_PREQUOTE
 * curl.OPT\_SOURCE\_QUOTE
 * curl.OPT\_TELNETOPTIONS


#### String options

The following options can be set to either a string or a nil value.  The
nil value is generally used to cancel or otherwise clear a previously
set option.

 * curl.OPT\_CAINFO
 * curl.OPT\_CAPATH
 * curl.OPT\_COOKIE
 * curl.OPT\_COOKIEFILE
 * curl.OPT\_COOKIEJAR
 * curl.OPT\_CUSTOMREQUEST
 * curl.OPT\_EGDSOCKET
 * curl.OPT\_ENCODING
 * curl.OPT\_FTPPORT
 * curl.OPT\_FTP\_ACCOUNT
 * curl.OPT\_INTERFACE
 * curl.OPT\_KRB4LEVEL
 * curl.OPT\_NETRC\_FILE
 * curl.OPT\_POSTFIELDS
 * curl.OPT\_PROXY
 * curl.OPT\_PROXYUSERPWD
 * curl.OPT\_RANDOM\_FILE
 * curl.OPT\_RANGE
 * curl.OPT\_REFERER
 * curl.OPT\_SOURCE\_URL
 * curl.OPT\_SOURCE\_USERPWD
 * curl.OPT\_SSLCERT
 * curl.OPT\_SSLCERTTYPE
 * curl.OPT\_SSLENGINE
 * curl.OPT\_SSLKEY
 * curl.OPT\_SSLKEYPASSWD
 * curl.OPT\_SSLKEYTYPE
 * curl.OPT\_SSL\_CIPHER\_LIST
 * curl.OPT\_URL
 * curl.OPT\_USERAGENT
 * curl.OPT\_USERPWD
 * curl.OPT\_WRITEINFO

#### Number options

The following options can be set to a numeric value

 * curl.OPT\_BUFFERSIZE
 * curl.OPT\_CLOSEPOLICY
 * curl.OPT\_CONNECTTIMEOUT
 * curl.OPT\_DNS\_CACHE\_TIMEOUT
 * curl.OPT\_FTPSSLAUTH
 * curl.OPT\_FTP\_RESPONSE\_TIMEOUT
 * curl.OPT\_FTP\_SSL
 * curl.OPT\_HTTPAUTH
 * curl.OPT\_HTTP\_VERSION
 * curl.OPT\_INFILESIZE
 * curl.OPT\_INFILESIZE\_LARGE
 * curl.OPT\_IPRESOLVE
 * curl.OPT\_LOW\_SPEED\_LIMIT
 * curl.OPT\_LOW\_SPEED\_TIME
 * curl.OPT\_MAXCONNECTS
 * curl.OPT\_MAXFILESIZE
 * curl.OPT\_MAXFILESIZE\_LARGE
 * curl.OPT\_MAXREDIRS
 * curl.OPT\_NETRC
 * curl.OPT\_PORT
 * curl.OPT\_POSTFIELDSIZE
 * curl.OPT\_POSTFIELDSIZE\_LARGE
 * curl.OPT\_PROXYAUTH
 * curl.OPT\_PROXYPORT
 * curl.OPT\_PROXYTYPE
 * curl.OPT\_RESUME\_FROM
 * curl.OPT\_RESUME\_FROM\_LARGE
 * curl.OPT\_SSLVERSION
 * curl.OPT\_SSL\_VERIFYHOST
 * curl.OPT\_TIMECONDITION
 * curl.OPT\_TIMEOUT
 * curl.OPT\_TIMEVALUE

#### Boolean options

The following options can be set to boolean true or false only (0 and 1
are not permitted and result in an error).

 * curl.OPT\_AUTOREFERER
 * curl.OPT\_COOKIESESSION
 * curl.OPT\_CRLF
 * curl.OPT\_DNS\_USE\_GLOBAL\_CACHE
 * curl.OPT\_FAILONERROR
 * curl.OPT\_FILETIME
 * curl.OPT\_FOLLOWLOCATION
 * curl.OPT\_FORBID\_REUSE
 * curl.OPT\_FRESH\_CONNECT
 * curl.OPT\_FTPAPPEND
 * curl.OPT\_FTPLISTONLY
 * curl.OPT\_FTP\_CREATE\_MISSING\_DIRS
 * curl.OPT\_FTP\_USE\_EPRT
 * curl.OPT\_FTP\_USE\_EPSV
 * curl.OPT\_HEADER
 * curl.OPT\_HTTPGET
 * curl.OPT\_HTTPPROXYTUNNEL
 * curl.OPT\_NOBODY
 * curl.OPT\_NOPROGRESS
 * curl.OPT\_NOSIGNAL
 * curl.OPT\_POST
 * curl.OPT\_PUT
 * curl.OPT\_SSLENGINE\_DEFAULT
 * curl.OPT\_SSL\_VERIFYPEER
 * curl.OPT\_TCP\_NODELAY
 * curl.OPT\_TRANSFERTEXT
 * curl.OPT\_UNRESTRICTED\_AUTH
 * curl.OPT\_UPLOAD
 * curl.OPT\_VERBOSE

## Constants

All enumeration types and define macros from libCURL 7.14.0 are exported
in curl namespace with the following names substitutions

 * CURL\_XXXX -> curl.XXXX
 * CURLXXXX -> curl.XXXX

## Attribution

These curl bindings are derived from the luacurl project whose origin is
http://luaforge.net/projects/luacurl/.  This source code is used by Message
Systems under the terms of the MIT license, which is reproduced below.  This
statement does not modify the terms of your license agreement with
Message Systems.

 * Copyright (c) 2003-2006 AVIQ Systems AG

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.


