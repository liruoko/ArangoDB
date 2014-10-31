////////////////////////////////////////////////////////////////////////////////
/// @brief Aql, expression executor
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

#ifndef ARANGODB_AQL_EXECUTOR_H
#define ARANGODB_AQL_EXECUTOR_H 1

#include "Basics/Common.h"
#include "Aql/Function.h"
#include "Aql/Variable.h"
#include "V8/v8-globals.h"

struct TRI_json_t;

namespace triagens {
  namespace basics {
    class StringBuffer;
  }

  namespace aql {

    struct AstNode;
    class Query;
    struct V8Expression;

// -----------------------------------------------------------------------------
// --SECTION--                                                    class Executor
// -----------------------------------------------------------------------------

    class Executor {

// -----------------------------------------------------------------------------
// --SECTION--                                        constructors / destructors
// -----------------------------------------------------------------------------

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief create the executor
////////////////////////////////////////////////////////////////////////////////

        Executor ();

////////////////////////////////////////////////////////////////////////////////
/// @brief destroy the executor
////////////////////////////////////////////////////////////////////////////////

        ~Executor ();

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

        struct TRI_json_t* executeExpression (Query*,
                                              AstNode const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief returns a reference to a built-in function
////////////////////////////////////////////////////////////////////////////////

        Function const* getFunctionByName (std::string const&);
    
////////////////////////////////////////////////////////////////////////////////
/// @brief checks if a V8 exception has occurred and throws an appropriate C++ 
/// exception from it if so
////////////////////////////////////////////////////////////////////////////////

        static void HandleV8Error (v8::TryCatch&,
                                   v8::Handle<v8::Value>&);

// -----------------------------------------------------------------------------
// --SECTION--                                                   private methods
// -----------------------------------------------------------------------------
      
      private:

////////////////////////////////////////////////////////////////////////////////
/// @brief generate JavaScript code for an arbitrary expression
////////////////////////////////////////////////////////////////////////////////

        void generateCodeExpression (AstNode const*); 

////////////////////////////////////////////////////////////////////////////////
/// @brief generates code for a string value
////////////////////////////////////////////////////////////////////////////////
        
        void generateCodeString (char const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief generates code for a string value
////////////////////////////////////////////////////////////////////////////////
        
        void generateCodeString (std::string const&);

////////////////////////////////////////////////////////////////////////////////
/// @brief generate JavaScript code for a list
////////////////////////////////////////////////////////////////////////////////

        void generateCodeList (AstNode const*); 

////////////////////////////////////////////////////////////////////////////////
/// @brief generate JavaScript code for an array
////////////////////////////////////////////////////////////////////////////////

        void generateCodeArray (AstNode const*); 

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
/// @brief generate JavaScript code for a variable
////////////////////////////////////////////////////////////////////////////////

        void generateCodeVariable (AstNode const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief generate JavaScript code for a full collection access
////////////////////////////////////////////////////////////////////////////////

        void generateCodeCollection (AstNode const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief generate JavaScript code for a call to a built-in function
////////////////////////////////////////////////////////////////////////////////

        void generateCodeFunctionCall (AstNode const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief generate JavaScript code for a user-defined function
////////////////////////////////////////////////////////////////////////////////

        void generateCodeUserFunctionCall (AstNode const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief generate JavaScript code for an expansion (i.e. [*] operator)
////////////////////////////////////////////////////////////////////////////////

        void generateCodeExpand (AstNode const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief generate JavaScript code for an expansion iterator
////////////////////////////////////////////////////////////////////////////////

        void generateCodeExpandIterator (AstNode const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief generate JavaScript code for a range (i.e. 1..10)
////////////////////////////////////////////////////////////////////////////////

        void generateCodeRange (AstNode const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief generate JavaScript code for a named attribute access
////////////////////////////////////////////////////////////////////////////////

        void generateCodeNamedAccess (AstNode const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief generate JavaScript code for a named attribute access
////////////////////////////////////////////////////////////////////////////////

        void generateCodeBoundAccess (AstNode const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief generate JavaScript code for an indexed attribute access
////////////////////////////////////////////////////////////////////////////////

        void generateCodeIndexedAccess (AstNode const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief generate JavaScript code for a node
////////////////////////////////////////////////////////////////////////////////

        void generateCodeNode (AstNode const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief create the string buffer
////////////////////////////////////////////////////////////////////////////////

        triagens::basics::StringBuffer* initializeBuffer ();

// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------

      private:

////////////////////////////////////////////////////////////////////////////////
/// @brief compile a V8 function from the code contained in the buffer
////////////////////////////////////////////////////////////////////////////////
  
        v8::Handle<v8::Value> compileExpression ();

////////////////////////////////////////////////////////////////////////////////
/// @brief a string buffer used for operations
////////////////////////////////////////////////////////////////////////////////

        triagens::basics::StringBuffer* _buffer;

////////////////////////////////////////////////////////////////////////////////
/// @brief AQL internal function names
////////////////////////////////////////////////////////////////////////////////

        static std::unordered_map<int, std::string const> const InternalFunctionNames;

////////////////////////////////////////////////////////////////////////////////
/// @brief AQL user-callable function names
////////////////////////////////////////////////////////////////////////////////

        static std::unordered_map<std::string, Function const> const FunctionNames;

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
