////////////////////////////////////////////////////////////////////////////////
/// @brief Ahuacatl, bind parameters
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2004-2013 triAGENS GmbH, Cologne, Germany
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
/// @author Jan Steemann
/// @author Copyright 2012-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef TRIAGENS_AHUACATL_AHUACATL_BIND_PARAMETER_H
#define TRIAGENS_AHUACATL_AHUACATL_BIND_PARAMETER_H 1

#include "Basics/Common.h"
#include "BasicsC/tri-strings.h"
#include "BasicsC/hashes.h"
#include "BasicsC/vector.h"
#include "BasicsC/associative.h"

#include "Ahuacatl/ahuacatl-ast-node.h"

struct TRI_aql_context_s;
struct TRI_json_s;

// -----------------------------------------------------------------------------
// --SECTION--                                                      public types
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief bind parameter container
////////////////////////////////////////////////////////////////////////////////

typedef struct TRI_aql_bind_parameter_s {
  char* _name;
  struct TRI_json_s* _value;
}
TRI_aql_bind_parameter_t;

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief hash bind parameter
////////////////////////////////////////////////////////////////////////////////

uint64_t TRI_HashBindParameterAql (TRI_associative_pointer_t*, void const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief comparison function used to determine bind parameter equality
////////////////////////////////////////////////////////////////////////////////

bool TRI_EqualBindParameterAql (TRI_associative_pointer_t*,
                                void const*,
                                void const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief free bind parameters
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeBindParametersAql (struct TRI_aql_context_s* const);

////////////////////////////////////////////////////////////////////////////////
/// @brief add bind parameters
////////////////////////////////////////////////////////////////////////////////

bool TRI_AddParameterValuesAql (struct TRI_aql_context_s* const,
                                const struct TRI_json_s* const);

////////////////////////////////////////////////////////////////////////////////
/// @brief validate bind parameters passed
////////////////////////////////////////////////////////////////////////////////

bool TRI_ValidateBindParametersAql (struct TRI_aql_context_s* const);

////////////////////////////////////////////////////////////////////////////////
/// @brief inject values of bind parameters into query
////////////////////////////////////////////////////////////////////////////////

bool TRI_InjectBindParametersAql (struct TRI_aql_context_s* const);

#endif

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
