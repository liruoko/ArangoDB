////////////////////////////////////////////////////////////////////////////////
/// @brief Aql, v8 context executor
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2014 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
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
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Jan Steemann
/// @author Copyright 2014, ArangoDB GmbH, Cologne, Germany
/// @author Copyright 2012-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGODB_AQL_V8_EXECUTOR_H
#define ARANGODB_AQL_V8_EXECUTOR_H 1

#include "Basics/Common.h"

struct TRI_json_s;

namespace triagens {
  namespace basics {
    class StringBuffer;
  }

  namespace aql {

    struct AstNode;
    struct V8Expression;

// -----------------------------------------------------------------------------
// --SECTION--                                                  class V8Executor
// -----------------------------------------------------------------------------

    class V8Executor {

// -----------------------------------------------------------------------------
// --SECTION--                                        constructors / destructors
// -----------------------------------------------------------------------------

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief create the executor
////////////////////////////////////////////////////////////////////////////////

        V8Executor ();

////////////////////////////////////////////////////////////////////////////////
/// @brief destroy the executor
////////////////////////////////////////////////////////////////////////////////

        ~V8Executor ();

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief generates an expression execution object
////////////////////////////////////////////////////////////////////////////////

        V8Expression* generateExpression (AstNode const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief executes an expression directly
////////////////////////////////////////////////////////////////////////////////

        struct TRI_json_s* executeExpression (AstNode const*);

// -----------------------------------------------------------------------------
// --SECTION--                                                   private methods
// -----------------------------------------------------------------------------
      
      private:

////////////////////////////////////////////////////////////////////////////////
/// @brief generate JavaScript code for an arbitrary expression
////////////////////////////////////////////////////////////////////////////////

        void generateCodeExpression (AstNode const*); 

////////////////////////////////////////////////////////////////////////////////
/// @brief generate JavaScript code for a unary operator
////////////////////////////////////////////////////////////////////////////////

        void generateCodeUnaryOperator (AstNode const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief generate JavaScript code for a binary operator
////////////////////////////////////////////////////////////////////////////////

        void generateCodeBinaryOperator (AstNode const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief generate JavaScript code for the ternary operator
////////////////////////////////////////////////////////////////////////////////

        void generateCodeTernaryOperator (AstNode const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief generate JavaScript code for a variable (read) access
////////////////////////////////////////////////////////////////////////////////

        void generateCodeReference (AstNode const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief generate JavaScript code for a full collection access
////////////////////////////////////////////////////////////////////////////////

        void generateCodeCollection (AstNode const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief generate JavaScript code for a function call
////////////////////////////////////////////////////////////////////////////////

        void generateCodeFunctionCall (AstNode const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief generate JavaScript code for a node
////////////////////////////////////////////////////////////////////////////////

        void generateCodeNode (AstNode const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief create the string buffer
////////////////////////////////////////////////////////////////////////////////

        triagens::basics::StringBuffer* initBuffer ();

////////////////////////////////////////////////////////////////////////////////
/// @brief executes the contents of the string buffer
////////////////////////////////////////////////////////////////////////////////

        struct TRI_json_s* execute ();

// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------

      private:

////////////////////////////////////////////////////////////////////////////////
/// @brief a string buffer used for operations
////////////////////////////////////////////////////////////////////////////////

        triagens::basics::StringBuffer* _buffer;

////////////////////////////////////////////////////////////////////////////////
/// @brief AQL function names
////////////////////////////////////////////////////////////////////////////////

        static std::unordered_map<int, std::string> const FunctionNames;


    };

  }
}

#endif

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
