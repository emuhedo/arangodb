////////////////////////////////////////////////////////////////////////////////
/// @brief http response
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2004-2012 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is triAGENS GmbH, Cologne, Germany
///
/// @author Dr. Frank Celler
/// @author Achim Brandt
/// @author Copyright 2008-2012, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef TRIAGENS_REST_HTTP_RESPONSE_H
#define TRIAGENS_REST_HTTP_RESPONSE_H 1

#include "Basics/Common.h"

#include "Basics/Dictionary.h"
#include "Basics/StringBuffer.h"

// -----------------------------------------------------------------------------
// --SECTION--                                              forward declarations
// -----------------------------------------------------------------------------

class PB_ArangoBatchMessage;

// -----------------------------------------------------------------------------
// --SECTION--                                                class HttpResponse
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Rest
/// @{
////////////////////////////////////////////////////////////////////////////////

namespace triagens {
  namespace rest {

////////////////////////////////////////////////////////////////////////////////
/// @brief http response
///
/// A http request handler is called to handle a http request. It returns its
/// answer as http response.
////////////////////////////////////////////////////////////////////////////////

    class HttpResponse {
      private:
        HttpResponse (HttpResponse const&);
        HttpResponse& operator= (HttpResponse const&);

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                      public types
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Rest
/// @{
////////////////////////////////////////////////////////////////////////////////

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief http response codes
////////////////////////////////////////////////////////////////////////////////

        enum HttpResponseCode {
          OK                   = 200,
          CREATED              = 201,
          ACCEPTED             = 202,
          PARTIAL              = 203,
          NO_CONTENT           = 204,

          MOVED_PERMANENTLY    = 301,
          FOUND                = 302,
          SEE_OTHER            = 303,
          NOT_MODIFIED         = 304,
          TEMPORARY_REDIRECT   = 307,

          BAD                  = 400,
          UNAUTHORIZED         = 401,
          PAYMENT              = 402,
          FORBIDDEN            = 403,
          NOT_FOUND            = 404,
          METHOD_NOT_ALLOWED   = 405,
          CONFLICT             = 409,
          PRECONDITION_FAILED  = 412,
          UNPROCESSABLE_ENTITY = 422,

          SERVER_ERROR         = 500,
          NOT_IMPLEMENTED      = 501,
          BAD_GATEWAY          = 502,
          SERVICE_UNAVAILABLE  = 503
        };

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                             static public methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Rest
/// @{
////////////////////////////////////////////////////////////////////////////////

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief http response string
///
/// Converts the response code to a string suitable for delivering to a http
/// client.
////////////////////////////////////////////////////////////////////////////////

        static string responseString (HttpResponseCode);

////////////////////////////////////////////////////////////////////////////////
/// @brief get http response code from string
///
/// Converts the response code string to the internal code
////////////////////////////////////////////////////////////////////////////////

        static HttpResponseCode responseCode (string const& str);

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Rest
/// @{
////////////////////////////////////////////////////////////////////////////////

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief constructs a new http response
////////////////////////////////////////////////////////////////////////////////

        HttpResponse ();

////////////////////////////////////////////////////////////////////////////////
/// @brief constructs a new http response
////////////////////////////////////////////////////////////////////////////////

        explicit
        HttpResponse (string const& lines);

////////////////////////////////////////////////////////////////////////////////
/// @brief constructs a new http response
////////////////////////////////////////////////////////////////////////////////

        explicit
        HttpResponse (HttpResponseCode);

////////////////////////////////////////////////////////////////////////////////
/// @brief deletes a http response
///
/// The descrutor will free the string buffers used. After the http response
/// is deleted, the string buffers returned by body() become invalid.
////////////////////////////////////////////////////////////////////////////////

        virtual ~HttpResponse ();

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Rest
/// @{
////////////////////////////////////////////////////////////////////////////////

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the response code
////////////////////////////////////////////////////////////////////////////////

        HttpResponseCode responseCode ();

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the content length
////////////////////////////////////////////////////////////////////////////////

        size_t contentLength ();

////////////////////////////////////////////////////////////////////////////////
/// @brief sets the content type
///
/// Sets the content type of the information of the body.
////////////////////////////////////////////////////////////////////////////////

        void setContentType (string const& contentType);

////////////////////////////////////////////////////////////////////////////////
/// @brief returns a header field
///
/// Returns the value of a header field with given name. If no header field
/// with the given name was specified by the client, the empty string is
/// returned.
////////////////////////////////////////////////////////////////////////////////

        string header (string const& field) const;

////////////////////////////////////////////////////////////////////////////////
/// @brief returns a header field
///
/// Returns the value of a header field with given name. If no header field
/// with the given name was specified by the client, the empty string is
/// returned. found is try if the client specified the header field.
////////////////////////////////////////////////////////////////////////////////

        string header (string const& field, bool& found) const;

////////////////////////////////////////////////////////////////////////////////
/// @brief returns all header fields
///
/// Returns all header fields
////////////////////////////////////////////////////////////////////////////////

        map<string, string> headers () const;

////////////////////////////////////////////////////////////////////////////////
/// @brief sets a header field
///
/// The key is automatically converted to lower case.
////////////////////////////////////////////////////////////////////////////////

        void setHeader (string const& key, string const& value);

////////////////////////////////////////////////////////////////////////////////
/// @brief sets many header fields
///
/// The key is automatically converted to lower case.
////////////////////////////////////////////////////////////////////////////////

        void setHeaders (string const& headers, bool includeLine0);

////////////////////////////////////////////////////////////////////////////////
/// @brief swaps data
////////////////////////////////////////////////////////////////////////////////

        HttpResponse* swap ();

////////////////////////////////////////////////////////////////////////////////
/// @brief writes the header
///
/// You should call writeHeader only after the body has been created.
////////////////////////////////////////////////////////////////////////////////

        void writeHeader (basics::StringBuffer*);

////////////////////////////////////////////////////////////////////////////////
/// @brief writes the message to a protocol buffer
///
/// You should call write only after the body has been created.
////////////////////////////////////////////////////////////////////////////////

        void write (PB_ArangoBatchMessage*);

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the size of the body
////////////////////////////////////////////////////////////////////////////////

        size_t bodySize ();

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the body
///
/// Returns a reference to the body. This reference is only valid as long as
/// http response exists. You can add data to the body by appending
/// information to the string buffer. Note that adding data to the body
/// invalidates any previously returned header. You must call header
/// again.
////////////////////////////////////////////////////////////////////////////////

        basics::StringBuffer& body ();

////////////////////////////////////////////////////////////////////////////////
/// @brief indicates a head response
///
/// In case of HEAD request, no body must be defined. However, the response
/// needs to know the size of body.
////////////////////////////////////////////////////////////////////////////////

        void headResponse (size_t);

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Rest
/// @{
////////////////////////////////////////////////////////////////////////////////

      private:

////////////////////////////////////////////////////////////////////////////////
/// @brief response code
////////////////////////////////////////////////////////////////////////////////

        HttpResponseCode _code;

////////////////////////////////////////////////////////////////////////////////
/// @brief headers dictionary
////////////////////////////////////////////////////////////////////////////////

        basics::Dictionary<char const*> _headers;

////////////////////////////////////////////////////////////////////////////////
/// @brief body
////////////////////////////////////////////////////////////////////////////////

        basics::StringBuffer _body;

////////////////////////////////////////////////////////////////////////////////
/// @brief head response flag
////////////////////////////////////////////////////////////////////////////////

        bool _isHeadResponse;

////////////////////////////////////////////////////////////////////////////////
/// @brief body size
////////////////////////////////////////////////////////////////////////////////

        size_t _bodySize;

////////////////////////////////////////////////////////////////////////////////
/// @brief freeable list
////////////////////////////////////////////////////////////////////////////////

        vector<char const*> _freeables;
    };
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

#endif

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}\\)"
// End:
