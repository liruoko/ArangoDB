////////////////////////////////////////////////////////////////////////////////
/// @brief AQL, expression
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2010-2014 triagens GmbH, Cologne, Germany
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
/// @author Max Neunhoeffer
/// @author Copyright 2014, triagens GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGODB_AQL_EXPRESSION_H
#define ARANGODB_AQL_EXPRESSION_H 1

#include "Basics/Common.h"
#include "Aql/AstNode.h"
#include "Basics/JsonHelper.h"

namespace triagens {
  namespace aql {

    struct AqlItem;
    struct AqlValue;

////////////////////////////////////////////////////////////////////////////////
/// @brief AqlExpression, used in execution plans and execution blocks
////////////////////////////////////////////////////////////////////////////////

    class Expression {

// -----------------------------------------------------------------------------
// --SECTION--                                        constructors / destructors
// -----------------------------------------------------------------------------
      
      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief constructor, using an AST start node
////////////////////////////////////////////////////////////////////////////////

        Expression (AstNode const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief destructor
////////////////////////////////////////////////////////////////////////////////

        ~Expression ();

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief get the underlying AST node
////////////////////////////////////////////////////////////////////////////////

        inline AstNode const* node () {
          return _node;
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief clone the expression, needed to clone execution plans
////////////////////////////////////////////////////////////////////////////////

        Expression* clone () {
          // We do not need to copy the _ast, since it is managed by the
          // query object and the memory management of the ASTs
          return new Expression(_node);
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief return a Json representation of the expression
////////////////////////////////////////////////////////////////////////////////

        triagens::basics::Json toJson (TRI_memory_zone_t* zone) const {
          return triagens::basics::Json(zone, _node->toJson(zone));
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief execute the expression
////////////////////////////////////////////////////////////////////////////////

        AqlValue* execute (AqlItem*);

// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------

      private:

////////////////////////////////////////////////////////////////////////////////
/// @brief the abstract syntax tree
////////////////////////////////////////////////////////////////////////////////

        // do we need a (possibly empty) subquery entry here?
        AstNode const*    _node;

    };

  }  // namespace triagens::aql
}  // namespace triagens

#endif

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End:

