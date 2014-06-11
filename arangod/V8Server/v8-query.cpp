////////////////////////////////////////////////////////////////////////////////
/// @brief V8-vocbase queries
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
/// @author Dr. Frank Celler
/// @author Copyright 2011-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "v8-query.h"

#include "BasicsC/logging.h"
#include "BasicsC/random.h"
#include "BasicsC/string-buffer.h"
#include "GeoIndex/geo-index.h"
#include "HashIndex/hash-index.h"
#include "FulltextIndex/fulltext-index.h"
#include "FulltextIndex/fulltext-result.h"
#include "FulltextIndex/fulltext-query.h"
#include "SkipLists/skiplistIndex.h"
#include "Utils/transactions.h"
#include "V8/v8-globals.h"
#include "V8/v8-conv.h"
#include "V8/v8-utils.h"
#include "V8Server/v8-vocbase.h"
#include "VocBase/edge-collection.h"
#include "VocBase/vocbase.h"

using namespace std;
using namespace triagens::basics;
using namespace triagens::arango;

// -----------------------------------------------------------------------------
// --SECTION--                                                   private defines
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief shortcut to wrap a shaped-json object in a read-only transaction
////////////////////////////////////////////////////////////////////////////////

#define WRAP_SHAPED_JSON(...) TRI_WrapShapedJson<V8ReadTransaction>(__VA_ARGS__)

// -----------------------------------------------------------------------------
// --SECTION--                                                  HELPER FUNCTIONS
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                     private types
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief geo coordinate container, also containing the distance
////////////////////////////////////////////////////////////////////////////////

typedef struct {
  double _distance;
  void const* _data;
}
geo_coordinate_distance_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief query types
////////////////////////////////////////////////////////////////////////////////

typedef enum {
  QUERY_EXAMPLE,
  QUERY_CONDITION
}
query_t;

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief return an empty result set
////////////////////////////////////////////////////////////////////////////////
  
static v8::Handle<v8::Value> EmptyResult () {
  v8::HandleScope scope;

  v8::Handle<v8::Object> result = v8::Object::New();
  result->Set(v8::String::New("documents"), v8::Array::New());
  result->Set(v8::String::New("total"), v8::Number::New(0));
  result->Set(v8::String::New("count"), v8::Number::New(0));

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief extracts skip and limit
////////////////////////////////////////////////////////////////////////////////

static void ExtractSkipAndLimit (v8::Arguments const& argv,
                                 size_t pos,
                                 TRI_voc_ssize_t& skip,
                                 TRI_voc_size_t& limit) {

  skip = TRI_QRY_NO_SKIP;
  limit = TRI_QRY_NO_LIMIT;

  if (pos < (size_t) argv.Length() && ! argv[(int) pos]->IsNull() && ! argv[(int) pos]->IsUndefined()) {
    skip = (TRI_voc_size_t) TRI_ObjectToDouble(argv[(int) pos]);
  }

  if (pos + 1 < (size_t) argv.Length() && ! argv[(int) pos + 1]->IsNull() && ! argv[(int) pos + 1]->IsUndefined()) {
    limit = (TRI_voc_ssize_t) TRI_ObjectToDouble(argv[(int) pos + 1]);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief calculates slice
////////////////////////////////////////////////////////////////////////////////

static void CalculateSkipLimitSlice (size_t length,
                                     TRI_voc_ssize_t skip,
                                     TRI_voc_size_t limit,
                                     size_t& s,
                                     size_t& e) {
  s = 0;
  e = length;

  // skip from the beginning
  if (0 < skip) {
    s = (size_t) skip;

    if (e < s) {
      s = (size_t) e;
    }
  }

  // skip from the end
  else if (skip < 0) {
    skip = -skip;

    if ((size_t) skip < e) {
      s = e - skip;
    }
  }

  // apply limit
  if (s + limit < e) {
    int64_t sum = (int64_t) s + (int64_t) limit;
    if (sum < (int64_t) e) {
      if (sum >= (int64_t) TRI_QRY_NO_LIMIT) {
        e = TRI_QRY_NO_LIMIT;
      }
      else {
        e = (size_t) sum;
      } 
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief cleans up the example object
////////////////////////////////////////////////////////////////////////////////

static void CleanupExampleObject (TRI_memory_zone_t* zone,
                                  size_t n,
                                  TRI_shape_pid_t* pids,
                                  TRI_shaped_json_t** values) {

  // clean shaped json objects
  for (size_t j = 0;  j < n;  ++j) {
    if (values[j] != 0) {
      TRI_FreeShapedJson(zone, values[j]);
    }
  }

  TRI_Free(TRI_UNKNOWN_MEM_ZONE, values);

  if (pids != 0) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, pids);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief sets up the example object
////////////////////////////////////////////////////////////////////////////////

static int SetupExampleObject (v8::Handle<v8::Object> const example,
                               TRI_shaper_t* shaper,
                               size_t& n,
                               TRI_shape_pid_t*& pids,
                               TRI_shaped_json_t**& values,
                               v8::Handle<v8::Object>* err) {

  // get own properties of example
  v8::Handle<v8::Array> names = example->GetOwnPropertyNames();
  n = names->Length();

  // setup storage
  pids = (TRI_shape_pid_t*) TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, n * sizeof(TRI_shape_pid_t), false);

  if (pids == 0) {
    // out of memory
    *err = TRI_CreateErrorObject(__FILE__, __LINE__, TRI_ERROR_OUT_OF_MEMORY);

    return TRI_ERROR_OUT_OF_MEMORY;
  }

  values = (TRI_shaped_json_t**) TRI_Allocate(TRI_UNKNOWN_MEM_ZONE,
                                              n * sizeof(TRI_shaped_json_t*), false);

  if (values == 0) {
    // out of memory
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, pids);
    pids = 0;
    *err = TRI_CreateErrorObject(__FILE__, __LINE__, TRI_ERROR_OUT_OF_MEMORY);

    return TRI_ERROR_OUT_OF_MEMORY;
  }

  // convert
  for (size_t i = 0;  i < n;  ++i) {
    v8::Handle<v8::Value> key = names->Get((uint32_t) i);
    v8::Handle<v8::Value> val = example->Get(key);

    // property initialise the memory
    values[i] = 0;

    TRI_Utf8ValueNFC keyStr(TRI_UNKNOWN_MEM_ZONE, key);

    if (*keyStr != 0) {
      pids[i] = shaper->lookupAttributePathByName(shaper, *keyStr);

      if (pids[i] == 0) {
        // no attribute path found. this means the result will be empty 
        CleanupExampleObject(shaper->_memoryZone, i, pids, values);
        return TRI_RESULT_ELEMENT_NOT_FOUND;
      }
      
      values[i] = TRI_ShapedJsonV8Object(val, shaper, false);

      if (values[i] == 0) {
        CleanupExampleObject(shaper->_memoryZone, i, pids, values);
        return TRI_RESULT_ELEMENT_NOT_FOUND;
      }
    }
    else {
      CleanupExampleObject(shaper->_memoryZone, i, pids, values);
      *err = TRI_CreateErrorObject(__FILE__,
                                   __LINE__,
                                   TRI_ERROR_BAD_PARAMETER,
                                   "cannot convert attribute path to UTF8");
      return TRI_ERROR_BAD_PARAMETER;
    }
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief sets up the skiplist operator for a skiplist condition query
////////////////////////////////////////////////////////////////////////////////

static TRI_index_operator_t* SetupConditionsSkiplist (TRI_index_t* idx,
                                                      TRI_shaper_t* shaper,
                                                      v8::Handle<v8::Object> conditions) {
  TRI_index_operator_t* lastOperator = 0;
  size_t numEq = 0;
  size_t lastNonEq = 0;

  TRI_json_t* parameters = TRI_CreateListJson(TRI_UNKNOWN_MEM_ZONE);

  if (parameters == 0) {
    return 0;
  }

  // iterate over all index fields
  for (size_t i = 1; i <= idx->_fields._length; ++i) {
    v8::Handle<v8::String> key = v8::String::New(idx->_fields._buffer[i - 1]);

    if (! conditions->HasOwnProperty(key)) {
      break;
    }
    v8::Handle<v8::Value> fieldConditions = conditions->Get(key);

    if (! fieldConditions->IsArray()) {
      // wrong data type for field conditions
      break;
    }

    // iterator over all conditions
    v8::Handle<v8::Array> values = v8::Handle<v8::Array>::Cast(fieldConditions);
    for (uint32_t j = 0; j < values->Length(); ++j) {
      v8::Handle<v8::Value> fieldCondition = values->Get(j);

      if (! fieldCondition->IsArray()) {
        // wrong data type for single condition
        goto MEM_ERROR;
      }

      v8::Handle<v8::Array> condition = v8::Handle<v8::Array>::Cast(fieldCondition);

      if (condition->Length() != 2) {
        // wrong number of values in single condition
        goto MEM_ERROR;
      }

      v8::Handle<v8::Value> op = condition->Get(0);
      v8::Handle<v8::Value> value = condition->Get(1);

      if (!op->IsString()) {
        // wrong operator type
        goto MEM_ERROR;
      }

      TRI_json_t* json = TRI_ObjectToJson(value);

      if (json == 0) {
        goto MEM_ERROR;
      }

      std::string opValue = TRI_ObjectToString(op);
      if (opValue == "==") {
        // equality comparison

        if (lastNonEq > 0) {
          TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
          goto MEM_ERROR;
        }

        TRI_PushBack3ListJson(TRI_UNKNOWN_MEM_ZONE, parameters, json);
        // creation of equality operator is deferred until it is finally needed
        ++numEq;
        break;
      }
      else {
        if (lastNonEq > 0 && lastNonEq != i) {
          // if we already had a range condition and a previous field, we cannot continue
          // because the skiplist interface does not support such queries
          TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
          goto MEM_ERROR;
        }

        TRI_index_operator_type_e opType;
        if (opValue == ">") {
          opType = TRI_GT_INDEX_OPERATOR;
        }
        else if (opValue == ">=") {
          opType = TRI_GE_INDEX_OPERATOR;
        }
        else if (opValue == "<") {
          opType = TRI_LT_INDEX_OPERATOR;
        }
        else if (opValue == "<=") {
          opType = TRI_LE_INDEX_OPERATOR;
        }
        else {
          // wrong operator type
          TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
          goto MEM_ERROR;
        }

        lastNonEq = i;

        TRI_json_t* cloned = TRI_CopyJson(TRI_UNKNOWN_MEM_ZONE, parameters);

        if (cloned == 0) {
          TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
          goto MEM_ERROR;
        }

        TRI_PushBack3ListJson(TRI_UNKNOWN_MEM_ZONE, cloned, json);

        if (numEq) {
          // create equality operator if one is in queue
          TRI_json_t* clonedParams = TRI_CopyJson(TRI_UNKNOWN_MEM_ZONE, parameters);

          if (clonedParams == 0) {
            TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, cloned);
            goto MEM_ERROR;
          }
          lastOperator = TRI_CreateIndexOperator(TRI_EQ_INDEX_OPERATOR, NULL, NULL, clonedParams, shaper, NULL, clonedParams->_value._objects._length, NULL);
          numEq = 0;
        }

        TRI_index_operator_t* current;

        // create the operator for the current condition
        current = TRI_CreateIndexOperator(opType, NULL, NULL, cloned, shaper, NULL, cloned->_value._objects._length, NULL);

        if (current == 0) {
          TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, cloned);
          goto MEM_ERROR;
        }

        if (lastOperator == 0) {
          lastOperator = current;
        }
        else {
          // merge the current operator with previous operators using logical AND
          TRI_index_operator_t* newOperator = TRI_CreateIndexOperator(TRI_AND_INDEX_OPERATOR, lastOperator, current, NULL, shaper, NULL, 2, NULL);

          if (newOperator == 0) {
            TRI_FreeIndexOperator(current);
            goto MEM_ERROR;
          }
          else {
            lastOperator = newOperator;
          }
        }
      }
    }

  }

  if (numEq) {
    // create equality operator if one is in queue
    TRI_ASSERT(lastOperator == 0);
    TRI_ASSERT(lastNonEq == 0);

    TRI_json_t* clonedParams = TRI_CopyJson(TRI_UNKNOWN_MEM_ZONE, parameters);

    if (clonedParams == 0) {
      goto MEM_ERROR;
    }
    lastOperator = TRI_CreateIndexOperator(TRI_EQ_INDEX_OPERATOR, NULL, NULL, clonedParams, shaper, NULL, clonedParams->_value._objects._length, NULL);
  }

  TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, parameters);

  return lastOperator;

MEM_ERROR:
  TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, parameters);

  if (lastOperator == 0) {
    TRI_FreeIndexOperator(lastOperator);
  }

  return 0;
}




////////////////////////////////////////////////////////////////////////////////
/// @brief sets up the bitarray operator for a bitarray condition query
////////////////////////////////////////////////////////////////////////////////


static TRI_json_t* SetupBitarrayAttributeValuesHelper (TRI_index_t* idx, v8::Handle<v8::Object> attributeValues) {
  TRI_json_t* parameters = TRI_CreateListJson(TRI_UNKNOWN_MEM_ZONE);


  // ........................................................................
  // No memory, no problem
  // ........................................................................

  if (parameters == 0) {
    return 0;
  }


  // ........................................................................
  // Client mucked something up?
  // ........................................................................

  if (! attributeValues->IsObject()) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, parameters);
    return 0;
  }


  // ........................................................................
  // Observe that the client can have sent any number of parameters which
  // do not match the list of attributes defined in the index.
  // These parameters are IGNORED -- no error is reported.
  // ........................................................................

  for (size_t i = 0; i < idx->_fields._length; ++i) {
    v8::Handle<v8::String> key = v8::String::New(idx->_fields._buffer[i]);
    TRI_json_t* json;

    // ......................................................................
    // The client may have sent values for all of the Attributes or for
    // a subset of them. If the value for an Attribute is missing, then we
    // assume that the client wishes to IGNORE the value of that Attribute.
    // In the later case, we add the json object 'TRI_JSON_UNUSED' to
    // indicate that this attribute is to be ignored. Notice that it is
    // possible to ignore all the attributes defined as part of the index.
    // ......................................................................


    if (attributeValues->HasOwnProperty(key)) {

      // ....................................................................
      // for this index attribute, there is such an attribute given as a
      // as a parameter by the client -- determine the value (or values)
      // of this attribute parameter and store it for later use in the
      // lookup
      // ....................................................................

      v8::Handle<v8::Value> value = attributeValues->Get(key);
      json = TRI_ObjectToJson(value);
      
      if (json == 0) {
        TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, parameters);
        return 0;
      }


      // ....................................................................
      // special case: if client sent {"x":[],...}, then we wrap this up
      // as {"x":[ [] ],...}.
      // ....................................................................

      if (json->_type == TRI_JSON_LIST) {
        if (json->_value._objects._length == 0) {
          TRI_json_t emptyList;
          emptyList._type = TRI_JSON_LIST;
          TRI_InitVector(&(emptyList._value._objects), TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_json_t));
          TRI_PushBack2ListJson(json, &emptyList);
        }
      }
    }

    else {
      // ....................................................................
      // for this index attribute we can not locate it in the list of parameters
      // sent to us by the client. Assign it an 'unused' (perhaps should be
      // renamed to 'unknown' or 'undefined').
      // ....................................................................
      json = (TRI_json_t*) TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_json_t), true);

      if (json == 0) {
        // OOM
        TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, parameters);
        return 0;
      }

      json->_type = TRI_JSON_UNUSED;
    }


    // ......................................................................
    // Check and ensure we have a json object defined before we store it.
    // ......................................................................

    TRI_ASSERT(json != 0);

    // ......................................................................
    // store it in an list json object -- eventually wil be stored as part
    // of the index operator.
    // ......................................................................

    TRI_PushBack3ListJson(TRI_UNKNOWN_MEM_ZONE, parameters, json);
  }


  return parameters;
}


static TRI_index_operator_t* SetupConditionsBitarrayHelper (TRI_index_t* idx,
                                                      TRI_shaper_t* shaper,
                                                      v8::Handle<v8::Object> condition) {

  v8::Handle<v8::Value> value;
  TRI_index_operator_type_e operatorType;

  // ........................................................................
  // Check the various operator conditions
  // ........................................................................


  // ........................................................................
  // Check for an 'AND' condition. The following are acceptable: '&', '&&' 'and'
  // ........................................................................
  if (condition->HasOwnProperty(v8::String::New("&"))) {
    operatorType = TRI_AND_INDEX_OPERATOR;
    value = condition->Get(v8::String::New("&"));
  }
  else if (condition->HasOwnProperty(v8::String::New("&&"))) {
    operatorType = TRI_AND_INDEX_OPERATOR;
    value = condition->Get(v8::String::New("&&"));
  }
  else if (condition->HasOwnProperty(v8::String::New("and"))) {
    operatorType = TRI_AND_INDEX_OPERATOR;
    value = condition->Get(v8::String::New("and"));
  }
  // ........................................................................
  // Check for an 'OR' condition. The following are acceptable: '|', '||' 'or'
  // ........................................................................
  else if (condition->HasOwnProperty(v8::String::New("|"))) {
    value = condition->Get(v8::String::New("|"));
    operatorType = TRI_OR_INDEX_OPERATOR;
  }
  else if (condition->HasOwnProperty(v8::String::New("||"))) {
    value = condition->Get(v8::String::New("||"));
    operatorType = TRI_OR_INDEX_OPERATOR;
  }
  else if (condition->HasOwnProperty(v8::String::New("or"))) {
    value = condition->Get(v8::String::New("or"));
    operatorType = TRI_OR_INDEX_OPERATOR;
  }
  // ........................................................................
  // Check for an 'NOT' condition. The following are acceptable: '!', 'not'
  // ........................................................................
  else if (condition->HasOwnProperty(v8::String::New("!"))) {
    value = condition->Get(v8::String::New("!"));
    operatorType = TRI_NOT_INDEX_OPERATOR;
  }
  else if (condition->HasOwnProperty(v8::String::New("not"))) {
    value = condition->Get(v8::String::New("not"));
    operatorType = TRI_NOT_INDEX_OPERATOR;
  }
  // ........................................................................
  // Check for an 'EQUAL' condition. The following are acceptable: '=', '==', 'eq'
  // ........................................................................
  else if (condition->HasOwnProperty(v8::String::New("=="))) {
    value = condition->Get(v8::String::New("=="));
    operatorType = TRI_EQ_INDEX_OPERATOR;
  }
  else if (condition->HasOwnProperty(v8::String::New("="))) {
    value = condition->Get(v8::String::New("="));
    operatorType = TRI_EQ_INDEX_OPERATOR;
  }
  else if (condition->HasOwnProperty(v8::String::New("eq"))) {
    value = condition->Get(v8::String::New("eq"));
    operatorType = TRI_EQ_INDEX_OPERATOR;
  }
  // ........................................................................
  // Check for an 'NOT EQUAL' condition. The following are acceptable: '!=', '<>, 'ne'
  // ........................................................................
  else if (condition->HasOwnProperty(v8::String::New("!="))) {
    value = condition->Get(v8::String::New("!="));
    operatorType = TRI_NE_INDEX_OPERATOR;
  }
  else if (condition->HasOwnProperty(v8::String::New("<>"))) {
    value = condition->Get(v8::String::New("<>"));
    operatorType = TRI_NE_INDEX_OPERATOR;
  }
  else if (condition->HasOwnProperty(v8::String::New("ne"))) {
    value = condition->Get(v8::String::New("ne"));
    operatorType = TRI_NE_INDEX_OPERATOR;
  }
  // ........................................................................
  // Check for an 'LESS THAN OR EQUAL' condition. The following are acceptable: '<=', 'le'
  // ........................................................................
  else if (condition->HasOwnProperty(v8::String::New("<="))) {
    value = condition->Get(v8::String::New("<="));
    operatorType = TRI_LE_INDEX_OPERATOR;
  }
  else if (condition->HasOwnProperty(v8::String::New("le"))) {
    value = condition->Get(v8::String::New("le"));
    operatorType = TRI_LE_INDEX_OPERATOR;
  }
  // ........................................................................
  // Check for an 'LESS THAN ' condition. The following are acceptable: '<', 'lt'
  // ........................................................................
  else if (condition->HasOwnProperty(v8::String::New("<"))) {
    value = condition->Get(v8::String::New("<"));
    operatorType = TRI_LT_INDEX_OPERATOR;
  }
  else if (condition->HasOwnProperty(v8::String::New("lt"))) {
    value = condition->Get(v8::String::New("lt"));
    operatorType = TRI_LT_INDEX_OPERATOR;
  }
  // ........................................................................
  // Check for an 'GREATER THAN OR EQUAL' condition. The following are acceptable: '>=', 'ge'
  // ........................................................................
  else if (condition->HasOwnProperty(v8::String::New(">="))) {
    value = condition->Get(v8::String::New(">="));
    operatorType = TRI_GE_INDEX_OPERATOR;
  }
  else if (condition->HasOwnProperty(v8::String::New("ge"))) {
    value = condition->Get(v8::String::New("ge"));
    operatorType = TRI_GE_INDEX_OPERATOR;
  }
  // ........................................................................
  // Check for an 'GREATER THAN ' condition. The following are acceptable: '>', 'gt'
  // ........................................................................
  else if (condition->HasOwnProperty(v8::String::New(">"))) {
    value = condition->Get(v8::String::New(">"));
    operatorType = TRI_GT_INDEX_OPERATOR;
  }
  else if (condition->HasOwnProperty(v8::String::New("gt"))) {
    value = condition->Get(v8::String::New("gt"));
    operatorType = TRI_GT_INDEX_OPERATOR;
  }
  // ........................................................................
  // We received an invalid condition. Most likely we are really expressing
  // a condition {"x":1} which should be BY_EXAMPLE rather than BY_CONDITION
  // ........................................................................
  else { // invalid operator index condition
    return 0;
  }

  TRI_index_operator_t* indexOperator = 0;

  // ........................................................................
  // Since we have a valid condition, act upon it
  // may require recursion
  // ........................................................................

  switch (operatorType) {

    case TRI_AND_INDEX_OPERATOR:
    case TRI_OR_INDEX_OPERATOR: {

      // ....................................................................
      // For both the 'AND' and 'OR' index operators, we require an array
      // with 2 elements for the value of the condition object. E.g. we
      // expect: {"&": [{"x":0},{"x":1}]} <-- this is a special "and" call
      //                                      see the ensureBitarray doc for
      //                                      more information.
      // More common is
      // expect: {"or": [{"x":0},{"x":1}]} <-- which means return all docs
      //                                       where attribute "x" has the
      //                                       value of 0 or 1.
      // To have "x" = 0 or "x" = 1 or "x" = 2 we expect:
      // {"or":[{"x":0},{"or":[{"x":1},{"x":2}]}]} or any valid iteration
      // of this. TODO: shortcut this with the "list" index operator
      // ....................................................................

      // ....................................................................
      // wrong data type for this condition -- we require [leftOperation,rightOperation]
      // ....................................................................

      if (!value->IsArray()) {
        return 0;
      }

      v8::Handle<v8::Array> andValues  = v8::Handle<v8::Array>::Cast(value);


      // ....................................................................
      // Check the length of the array to ensure that it is exactly 2
      // ....................................................................

      if (andValues->Length() != 2) {
        return 0;
      }

      v8::Handle<v8::Value> leftValue  = andValues->Get(0);
      v8::Handle<v8::Value> rightValue = andValues->Get(1);

      if (!leftValue->IsObject() || !rightValue->IsObject()) {
        return 0;
      }

      v8::Handle<v8::Object> leftObject  = v8::Handle<v8::Object>::Cast(leftValue);
      v8::Handle<v8::Object> rightObject = v8::Handle<v8::Object>::Cast(rightValue);


      // ....................................................................
      // recurse the left and right operators
      // ....................................................................

      TRI_index_operator_t* leftOp  = SetupConditionsBitarrayHelper(idx, shaper, leftObject);
      TRI_index_operator_t* rightOp = SetupConditionsBitarrayHelper(idx, shaper, rightObject);

      if (leftOp == 0 || rightOp == 0) {
        TRI_FreeIndexOperator(leftOp);
        TRI_FreeIndexOperator(rightOp);
        return 0;
      }

      indexOperator = TRI_CreateIndexOperator(operatorType, leftOp, rightOp, NULL, shaper, NULL, 0, NULL);
      break;
    }

    case TRI_NOT_INDEX_OPERATOR: {

      // ....................................................................
      // wrong data type for this condition -- we require {...} which becomes
      // the left object for not operator.
      // ....................................................................

      if (!value->IsObject()) {
        return 0;
      }

      v8::Handle<v8::Object> leftObject  = v8::Handle<v8::Object>::Cast(value);


      // ....................................................................
      // recurse the left and only operator
      // ....................................................................
      TRI_index_operator_t* leftOp  = SetupConditionsBitarrayHelper(idx, shaper, leftObject);

      if (leftOp == 0) {
        return 0;
      }

      indexOperator = TRI_CreateIndexOperator(operatorType, leftOp, NULL, NULL, shaper, NULL, 0, NULL);
      break;
    }

    case TRI_EQ_INDEX_OPERATOR:
    case TRI_NE_INDEX_OPERATOR:
    case TRI_LE_INDEX_OPERATOR:
    case TRI_LT_INDEX_OPERATOR:
    case TRI_GE_INDEX_OPERATOR:
    case TRI_GT_INDEX_OPERATOR: {

      v8::Handle<v8::Object> leftObject  = v8::Handle<v8::Object>::Cast(value);
      TRI_json_t* parameters = SetupBitarrayAttributeValuesHelper(idx, leftObject);

      if (parameters == 0) {
        return 0;
      }

      indexOperator = TRI_CreateIndexOperator(operatorType, NULL, NULL, parameters, shaper, NULL, parameters->_value._objects._length, NULL);
      break;
    }

  } // end of switch (operatorType)

  return indexOperator;
}


static TRI_index_operator_t* SetupConditionsBitarray (TRI_index_t* idx,
                                                      TRI_shaper_t* shaper,
                                                      v8::Handle<v8::Object> condition) {
  TRI_index_operator_t* indexOperator = SetupConditionsBitarrayHelper(idx, shaper, condition);
  return indexOperator;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief sets up the skiplist operator for a skiplist example query
///
/// this will set up a JSON container with the example values as a list
/// at the end, one skiplist equality operator is created for the entire list
////////////////////////////////////////////////////////////////////////////////

static TRI_index_operator_t* SetupExampleSkiplist (TRI_index_t* idx,
                                                   TRI_shaper_t* shaper,
                                                   v8::Handle<v8::Object> example) {
  TRI_json_t* parameters = TRI_CreateListJson(TRI_UNKNOWN_MEM_ZONE);

  if (parameters == 0) {
    return 0;
  }

  for (size_t i = 0; i < idx->_fields._length; ++i) {
    v8::Handle<v8::String> key = v8::String::New(idx->_fields._buffer[i]);

    if (! example->HasOwnProperty(key)) {
      break;
    }

    v8::Handle<v8::Value> value = example->Get(key);

    TRI_json_t* json = TRI_ObjectToJson(value);

    if (json == 0) {
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, parameters);

      return 0;
    }

    TRI_PushBack3ListJson(TRI_UNKNOWN_MEM_ZONE, parameters, json);
  }

  if (parameters->_value._objects._length > 0) {
    // example means equality comparisons only
    return TRI_CreateIndexOperator(TRI_EQ_INDEX_OPERATOR, NULL, NULL, parameters, shaper, NULL, parameters->_value._objects._length, NULL);
  }

  TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, parameters);
  return 0;
}


////////////////////////////////////////////////////////////////////////////////
/// @brief creates an index operator for a bitarray example query
///
/// this will set up a JSON container with the example values as a list
/// at the end, one skiplist equality operator is created for the entire list
////////////////////////////////////////////////////////////////////////////////

static TRI_index_operator_t* SetupExampleBitarray (TRI_index_t* idx, TRI_shaper_t* shaper, v8::Handle<v8::Object> example) {
  TRI_json_t* parameters = SetupBitarrayAttributeValuesHelper(idx, example);

  if (parameters == 0) {
    return 0;
  }

  // for an example query, we can only assume equality operator is required.
  return TRI_CreateIndexOperator(TRI_EQ_INDEX_OPERATOR, NULL, NULL, parameters, shaper, NULL, parameters->_value._objects._length, NULL);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys the example object for a hash index
////////////////////////////////////////////////////////////////////////////////

static void DestroySearchValue (TRI_memory_zone_t* zone,
                                TRI_index_search_value_t& value) {
  size_t n;

  n = value._length;

  for (size_t j = 0;  j < n;  ++j) {
    TRI_DestroyShapedJson(zone, &value._values[j]);
  }

  TRI_Free(TRI_CORE_MEM_ZONE, value._values);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief sets up the example object for a hash index
////////////////////////////////////////////////////////////////////////////////

static int SetupSearchValue (TRI_vector_t const* paths,
                             v8::Handle<v8::Object> example,
                             TRI_shaper_t* shaper,
                             TRI_index_search_value_t& result,
                             v8::Handle<v8::Object>* err) {
  size_t n;

  // extract attribute paths
  n = paths->_length;

  // setup storage
  result._length = n;
  result._values = (TRI_shaped_json_t*) TRI_Allocate(TRI_CORE_MEM_ZONE,
                                                     n * sizeof(TRI_shaped_json_t),
                                                     true);

  // convert
  for (size_t i = 0;  i < n;  ++i) {
    TRI_shape_pid_t pid = * (TRI_shape_pid_t*) TRI_AtVector(paths, i);

    TRI_ASSERT(pid != 0);
    char const* name = TRI_AttributeNameShapePid(shaper, pid);

    if (name == NULL) {
      DestroySearchValue(shaper->_memoryZone, result);
      *err = TRI_CreateErrorObject(__FILE__,
                                   __LINE__,
                                   TRI_ERROR_INTERNAL,
                                   "shaper failed");
      return TRI_ERROR_BAD_PARAMETER;
    }

    v8::Handle<v8::String> key = v8::String::New(name);
    int res;

    if (example->HasOwnProperty(key)) {
      v8::Handle<v8::Value> val = example->Get(key);

      res = TRI_FillShapedJsonV8Object(val, &result._values[i], shaper, false); 
    }
    else {
      res = TRI_FillShapedJsonV8Object(v8::Null(), &result._values[i], shaper, false); 
    }

    if (res != TRI_ERROR_NO_ERROR) {
      DestroySearchValue(shaper->_memoryZone, result);

      if (res != TRI_RESULT_ELEMENT_NOT_FOUND) {
        *err = TRI_CreateErrorObject(__FILE__,
                                     __LINE__,
                                     res,
                                     "cannot convert value to JSON");
      }
      return res;
    }
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief execute a skiplist query (by condition or by example)
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ExecuteSkiplistQuery (v8::Arguments const& argv,
                                                   std::string const& signature,
                                                   const query_t type) {
  v8::HandleScope scope;

  // expecting index, example, skip, and limit
  if (argv.Length() < 2) {
    TRI_V8_EXCEPTION_USAGE(scope, signature.c_str());
  }

  if (! argv[1]->IsObject()) {
    std::string msg;

    if (type == QUERY_EXAMPLE) {
      msg = "<example> must be an object";
    }
    else {
      msg = "<conditions> must be an object";
    }

    TRI_V8_TYPE_ERROR(scope, msg.c_str());
  }


  TRI_vocbase_col_t const* col;
  col = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), TRI_GetVocBaseColType());

  if (col == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
  TRI_SHARDING_COLLECTION_NOT_YET_IMPLEMENTED(scope, col);

  V8ReadTransaction trx(col->_vocbase, col->_cid);
  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  v8::Handle<v8::Object> err;

  TRI_document_collection_t* document = trx.documentCollection();
  TRI_shaper_t* shaper = document->getShaper();

  // extract skip and limit
  TRI_voc_ssize_t skip;
  TRI_voc_size_t limit;

  ExtractSkipAndLimit(argv, 2, skip, limit);

  // setup result
  v8::Handle<v8::Object> result = v8::Object::New();

  v8::Handle<v8::Array> documents = v8::Array::New();
  result->Set(v8::String::New("documents"), documents);

  // .............................................................................
  // inside a read transaction
  // .............................................................................

  trx.lockRead();


  // extract the index
  TRI_index_t* idx = TRI_LookupIndexByHandle(trx.resolver(), col, argv[0], false, &err);

  if (idx == nullptr) {
    return scope.Close(v8::ThrowException(err));
  }

  if (idx->_type != TRI_IDX_TYPE_SKIPLIST_INDEX) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_NO_INDEX);
  }

  TRI_index_operator_t* skiplistOperator;
  v8::Handle<v8::Object> values = argv[1]->ToObject();

  if (type == QUERY_EXAMPLE) {
    skiplistOperator = SetupExampleSkiplist(idx, shaper, values);
  }
  else {
    skiplistOperator = SetupConditionsSkiplist(idx, shaper, values);
  }

  if (skiplistOperator == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_BAD_PARAMETER);
  }

  TRI_skiplist_iterator_t* skiplistIterator = TRI_LookupSkiplistIndex(idx, skiplistOperator);

  if (skiplistIterator == 0) {
    int res = TRI_errno();
    if (res == TRI_RESULT_ELEMENT_NOT_FOUND) {
      return scope.Close(EmptyResult());
    }
     
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_NO_INDEX);
  }

  TRI_voc_ssize_t total = 0;
  TRI_voc_size_t count = 0;
  bool error = false;

  if (trx.orderBarrier(trx.trxCollection()) == nullptr) {
    TRI_FreeSkiplistIterator(skiplistIterator);
    TRI_V8_EXCEPTION(scope, TRI_ERROR_OUT_OF_MEMORY);
  }

  while (limit > 0) {
    TRI_skiplist_index_element_t* indexElement = skiplistIterator->_next(skiplistIterator);

    if (indexElement == NULL) {
      break;
    }

    ++total;

    if (total > skip && count < limit) {
      v8::Handle<v8::Value> doc = WRAP_SHAPED_JSON(trx, 
                                                   col->_cid, 
                                                   (TRI_doc_mptr_t const*) indexElement->_document); 

      if (doc.IsEmpty()) {
        error = true;
        break;
      }
      else {
        documents->Set(count, doc);

        if (++count >= limit) {
          break;
        }
      }

    }
  }

  trx.finish(res);

  // .............................................................................
  // outside a write transaction
  // .............................................................................

  // free data allocated by skiplist index result
  TRI_FreeSkiplistIterator(skiplistIterator);

  result->Set(v8::String::New("total"), v8::Number::New((double) total));
  result->Set(v8::String::New("count"), v8::Number::New(count));

  if (error) {
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  return scope.Close(result);
}


////////////////////////////////////////////////////////////////////////////////
// Example of a filter associated with an interator
////////////////////////////////////////////////////////////////////////////////

static bool BitarrayFilterExample (TRI_index_iterator_t* indexIterator) {
  TRI_doc_mptr_t* indexElement;
  TRI_bitarray_index_t* baIndex;
    
  indexElement = (TRI_doc_mptr_t*) indexIterator->_next(indexIterator);

  if (indexElement == nullptr) {
    return false;
  }

  baIndex = (TRI_bitarray_index_t*) indexIterator->_index;

  if (baIndex == nullptr) {
    return false;
  }
    
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief execute a bitarray index query (by condition or by example)
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ExecuteBitarrayQuery (v8::Arguments const& argv,
                                                   std::string const& signature,
                                                   const query_t type) {
  v8::HandleScope scope;

  // ...........................................................................
  // Check the parameters, expecting index, example, skip, and limit
  // e.g. ("110597/962565", {"x":1}, null, null)
  // ...........................................................................

  if (argv.Length() < 2) {
    TRI_V8_EXCEPTION_USAGE(scope, signature.c_str());
  }

  // ...........................................................................
  // Check that the second parameter is an associative array (json object)
  // ...........................................................................

  if (! argv[1]->IsObject()) {
    std::string msg;

    if (type == QUERY_EXAMPLE) {
      msg = "<example> must be an object";
    }
    else {
      msg = "<conditions> must be an object";
    }

    TRI_V8_EXCEPTION_PARAMETER(scope, msg);
  }


  // .............................................................................
  // extract skip and limit
  // .............................................................................

  TRI_voc_ssize_t skip;
  TRI_voc_size_t limit;
  ExtractSkipAndLimit(argv, 2, skip, limit);


  TRI_vocbase_col_t const* col;
  col = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), TRI_GetVocBaseColType());

  if (col == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
  TRI_SHARDING_COLLECTION_NOT_YET_IMPLEMENTED(scope, col);

  V8ReadTransaction trx(col->_vocbase, col->_cid);

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  TRI_document_collection_t* document = trx.documentCollection();
  TRI_shaper_t* shaper = document->getShaper();

  // .............................................................................
  // Create the json object result which stores documents located
  // .............................................................................

  v8::Handle<v8::Object> result = v8::Object::New();

  // .............................................................................
  // Create the array to store documents located
  // .............................................................................

  v8::Handle<v8::Array> documents = v8::Array::New();
  result->Set(v8::String::New("documents"), documents);


  // .............................................................................
  // inside a read transaction
  // .............................................................................

  trx.lockRead();

  // .............................................................................
  // extract the index
  // .............................................................................

  v8::Handle<v8::Object> err;
  TRI_index_t* idx = TRI_LookupIndexByHandle(trx.resolver(), col, argv[0], false, &err);

  if (idx == nullptr) {
    return scope.Close(v8::ThrowException(err));
  }

  if (idx->_type != TRI_IDX_TYPE_BITARRAY_INDEX) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_NO_INDEX);
  }
  
  if (trx.orderBarrier(trx.trxCollection()) == nullptr) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_OUT_OF_MEMORY);
  }


  TRI_index_operator_t* indexOperator;
  v8::Handle<v8::Object> values = argv[1]->ToObject();
  if (type == QUERY_EXAMPLE) {
    indexOperator = SetupExampleBitarray(idx, shaper, values);
  }
  else {
    indexOperator = SetupConditionsBitarray(idx, shaper, values);
  }

  if (indexOperator == 0) { // something wrong
    TRI_V8_EXCEPTION(scope, TRI_ERROR_BAD_PARAMETER);
  }

  // .............................................................................
  // attempt to locate the documents
  // .............................................................................

  TRI_index_iterator_t* indexIterator = TRI_LookupBitarrayIndex(idx, indexOperator, BitarrayFilterExample);

  // .............................................................................
  // Take care of the case where the index iterator is returned as NULL -- may
  // occur when some catastrophic error occurs.
  // .............................................................................
 

  TRI_voc_ssize_t total = 0;
  TRI_voc_size_t count = 0;
  bool error = false;

  if (indexIterator != NULL) {
    while (limit > 0) {
      TRI_doc_mptr_t* data = (TRI_doc_mptr_t*) indexIterator->_next(indexIterator);

      if (data == NULL) {
        break;
      }

      ++total;

      if (total > skip && count < limit) {
        v8::Handle<v8::Value> doc = WRAP_SHAPED_JSON(trx, col->_cid, data);

        if (doc.IsEmpty()) {
          error = true;
          break;
        }
        else {
          documents->Set(count, doc);
          if (++count >= limit) {
            break;
          }
        }
      }
    }

    // free data allocated by index result
    TRI_FreeIndexIterator(indexIterator);
  }

  else {
    LOG_WARNING("index iterator returned with a NULL value in ExecuteBitarrayQuery");
    // return an empty list
  }

  trx.finish(res);

  // .............................................................................
  // outside a write transaction
  // .............................................................................


  result->Set(v8::String::New("total"), v8::Number::New((double) total));
  result->Set(v8::String::New("count"), v8::Number::New(count));
  
  if (error) {
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a geo result
////////////////////////////////////////////////////////////////////////////////

static int StoreGeoResult (V8ReadTransaction& trx,
                           TRI_vocbase_col_t const* collection,
                           GeoCoordinates* cors,
                           v8::Handle<v8::Array>& documents,
                           v8::Handle<v8::Array>& distances) {
  GeoCoordinate* end;
  GeoCoordinate* ptr;
  double* dtr;
  geo_coordinate_distance_t* gtr;
  geo_coordinate_distance_t* tmp;
  uint32_t i;
  
  if (trx.orderBarrier(trx.trxCollection()) == nullptr) {
    GeoIndex_CoordinatesFree(cors);
    return TRI_ERROR_OUT_OF_MEMORY;
  }
  
  // sort the result
  size_t n = cors->length;

  if (n == 0) {
    GeoIndex_CoordinatesFree(cors);
    return TRI_ERROR_NO_ERROR;
  }

  gtr = (tmp = (geo_coordinate_distance_t*) TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(geo_coordinate_distance_t) * n, false));

  if (gtr == nullptr) {
    GeoIndex_CoordinatesFree(cors);

    return TRI_ERROR_OUT_OF_MEMORY;
  }
  
  geo_coordinate_distance_t* gnd = tmp + n;

  ptr = cors->coordinates;
  end = cors->coordinates + n;

  dtr = cors->distances;

  for (;  ptr < end;  ++ptr, ++dtr, ++gtr) {
    gtr->_distance = *dtr;
    gtr->_data = ptr->data;
  }

  GeoIndex_CoordinatesFree(cors);

  // sort result by distance
  auto compareSort = [] (geo_coordinate_distance_t const& left, geo_coordinate_distance_t const& right) {
    return left._distance < right._distance;
  };
  std::sort(tmp, gnd, compareSort);

  // copy the documents
  bool error = false;
  for (gtr = tmp, i = 0;  gtr < gnd;  ++gtr, ++i) {
    v8::Handle<v8::Value> doc = WRAP_SHAPED_JSON(trx, collection->_cid, (TRI_doc_mptr_t const*) gtr->_data);

    if (doc.IsEmpty()) {
      error = true;
      break;
    }

    documents->Set(i, doc);
    distances->Set(i, v8::Number::New(gtr->_distance));
  }

  TRI_Free(TRI_UNKNOWN_MEM_ZONE, tmp);
  
  if (error) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }

  return TRI_ERROR_NO_ERROR;
}

// -----------------------------------------------------------------------------
// --SECTION--                                                   QUERY FUNCTIONS
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up edges for given direction
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> EdgesQuery (TRI_edge_direction_e direction, 
                                         v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_col_t const* col;
  col = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), TRI_GetVocBaseColType());

  if (col == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
  TRI_SHARDING_COLLECTION_NOT_YET_IMPLEMENTED(scope, col);

  if (col->_type != TRI_COL_TYPE_EDGE) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_COLLECTION_TYPE_INVALID);
  }

  V8ReadTransaction trx(col->_vocbase, col->_cid);

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  TRI_document_collection_t* document = trx.documentCollection();

  // first and only argument schould be a list of document idenfifier
  if (argv.Length() != 1) {
    switch (direction) {
      case TRI_EDGE_IN:
        TRI_V8_EXCEPTION_USAGE(scope, "inEdges(<vertices>)");

      case TRI_EDGE_OUT:
        TRI_V8_EXCEPTION_USAGE(scope, "outEdges(<vertices>)");

      case TRI_EDGE_ANY:
      default: {
        TRI_V8_EXCEPTION_USAGE(scope, "edges(<vertices>)");
      }
    }
  }

  // setup result
  v8::Handle<v8::Array> documents = v8::Array::New();

  // .............................................................................
  // inside a read transaction
  // .............................................................................

  trx.lockRead();

  uint32_t count = 0;
  bool error = false;

  if (trx.orderBarrier(trx.trxCollection()) == nullptr) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_OUT_OF_MEMORY);
  }

  // argument is a list of vertices
  if (argv[0]->IsArray()) {
    v8::Handle<v8::Array> vertices = v8::Handle<v8::Array>::Cast(argv[0]);
    const uint32_t len = vertices->Length();

    for (uint32_t i = 0;  i < len; ++i) {
      std::vector<TRI_doc_mptr_copy_t> edges;
      TRI_voc_cid_t cid;
      TRI_voc_key_t key = 0;

      res = TRI_ParseVertex(trx.resolver(), cid, key, vertices->Get(i), true);

      if (res != TRI_ERROR_NO_ERROR) {
        // error is just ignored
        continue;
      }

      edges = TRI_LookupEdgesDocumentCollection(document, direction, cid, key);

      if (key != 0) {
       TRI_FreeString(TRI_CORE_MEM_ZONE, key);
      }

      for (size_t j = 0;  j < edges.size();  ++j) {
        v8::Handle<v8::Value> doc = WRAP_SHAPED_JSON(trx, col->_cid, &edges[j]);

        if (doc.IsEmpty()) {
          // error
          error = true;
          break;
        }
        else {
          documents->Set(count, doc);
          ++count;
        }

      }

      if (error) {
        break;
      }
    }
    trx.finish(res);
  }

  // argument is a single vertex
  else {
    std::vector<TRI_doc_mptr_copy_t> edges;

    TRI_voc_key_t key = nullptr;
    TRI_voc_cid_t cid;

    res = TRI_ParseVertex(trx.resolver(), cid, key, argv[0], true);

    if (res != TRI_ERROR_NO_ERROR) {
      TRI_V8_EXCEPTION(scope, res);
    }

    edges = TRI_LookupEdgesDocumentCollection(document, direction, cid, key);

    trx.finish(res);

    if (key != nullptr) {
      TRI_FreeString(TRI_CORE_MEM_ZONE, key);
    }

    for (size_t j = 0;  j < edges.size();  ++j) {
      v8::Handle<v8::Value> doc = WRAP_SHAPED_JSON(trx, col->_cid, &edges[j]);

      if (doc.IsEmpty()) {
        error = true;
        break;
      }
      else {
        documents->Set(count, doc);
        ++count;
      }
    }
  }

  // .............................................................................
  // outside a read transaction
  // .............................................................................
  
  if (error) {
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  return scope.Close(documents);
}

// -----------------------------------------------------------------------------
// --SECTION--                                              javascript functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief selects all documents from a collection
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_AllQuery (v8::Arguments const& argv) {
  v8::HandleScope scope;

  // expecting two arguments
  if (argv.Length() != 2) {
    TRI_V8_EXCEPTION_USAGE(scope, "ALL(<skip>, <limit>)");
  }

  TRI_vocbase_col_t const* col;
  col = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), TRI_GetVocBaseColType());

  if (col == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
  TRI_SHARDING_COLLECTION_NOT_YET_IMPLEMENTED(scope, col);

  // extract skip and limit
  TRI_voc_ssize_t skip;
  TRI_voc_size_t limit;
  ExtractSkipAndLimit(argv, 0, skip, limit);

  uint32_t total = 0;
  vector<TRI_doc_mptr_copy_t> docs;

  V8ReadTransaction trx(col->_vocbase, col->_cid);

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  res = trx.read(docs, skip, limit, &total);

  res = trx.finish(res);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  size_t const n = docs.size();
  uint32_t count = 0;
 
  // setup result
  v8::Handle<v8::Object> result = v8::Object::New();
  v8::Handle<v8::Array> documents = v8::Array::New((int) n);
  // reserve full capacity in one go
  result->Set(v8::String::New("documents"), documents);

  for (size_t i = 0; i < n; ++i) {
    v8::Handle<v8::Value> doc = WRAP_SHAPED_JSON(trx, col->_cid, &docs[i]);

    if (doc.IsEmpty()) {
      TRI_V8_EXCEPTION_MEMORY(scope);
    }
    else {
      documents->Set(count++, doc);
    }
  }

  result->Set(v8::String::New("total"), v8::Number::New(total));
  result->Set(v8::String::New("count"), v8::Number::New(count));
  
  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief selects documents from a collection, using an offset into the
/// primary index. this can be used for incremental access
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_OffsetQuery (v8::Arguments const& argv) {
  v8::HandleScope scope;

  // expecting two arguments
  if (argv.Length() != 4) {
    TRI_V8_EXCEPTION_USAGE(scope, "OFFSET(<internalSkip>, <batchSize>, <skip>, <limit>)");
  }

  TRI_vocbase_col_t const* col;
  col = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), TRI_GetVocBaseColType());

  if (col == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
  TRI_SHARDING_COLLECTION_NOT_YET_IMPLEMENTED(scope, col);

  TRI_voc_size_t internalSkip = (TRI_voc_size_t) TRI_ObjectToDouble(argv[0]);
  TRI_voc_size_t batchSize = (TRI_voc_size_t) TRI_ObjectToDouble(argv[1]);

  // extract skip and limit
  TRI_voc_ssize_t skip;
  TRI_voc_size_t limit;
  ExtractSkipAndLimit(argv, 2, skip, limit);

  uint32_t total = 0;
  vector<TRI_doc_mptr_copy_t> docs;

  V8ReadTransaction trx(col->_vocbase, col->_cid);

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  res = trx.readOffset(docs, internalSkip, batchSize, skip, &total);
  res = trx.finish(res);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  size_t const n = docs.size();
  uint32_t count = 0;
  
  // setup result
  v8::Handle<v8::Object> result = v8::Object::New();
  v8::Handle<v8::Array> documents = v8::Array::New((int) n);
  // reserve full capacity in one go
  result->Set(v8::String::New("documents"), documents);

  for (size_t i = 0; i < n; ++i) {
    v8::Handle<v8::Value> document = WRAP_SHAPED_JSON(trx, col->_cid, &docs[i]);

    if (document.IsEmpty()) {
      TRI_V8_EXCEPTION_MEMORY(scope);
    }
    else {
      documents->Set(count++, document);
    }
  }

  result->Set(v8::String::New("total"), v8::Number::New(total));
  result->Set(v8::String::New("count"), v8::Number::New(count));
  result->Set(v8::String::New("skip"), v8::Number::New(internalSkip));
  
  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief selects a random document
///
/// @FUN{@FA{collection}.any()}
///
/// The @FN{any} method returns a random document from the collection.  It returns
/// @LIT{null} if the collection is empty.
///
/// @EXAMPLES
///
/// @code
/// arangod> db.example.any()
/// { "_id" : "example/222716379559", "_rev" : "222716379559", "Hello" : "World" }
/// @endcode
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_AnyQuery (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_col_t const* col;
  col = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), TRI_GetVocBaseColType());

  if (col == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }

  TRI_doc_mptr_copy_t document;

  V8ReadTransaction trx(col->_vocbase, col->_cid);

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  res = trx.readRandom(&document);
  res = trx.finish(res);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  if (document.getDataPtr() == nullptr) {  // PROTECTED by trx here
    return scope.Close(v8::Null());
  }

  v8::Handle<v8::Value> doc = WRAP_SHAPED_JSON(trx, col->_cid, &document);
  
  if (doc.IsEmpty()) {
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  return scope.Close(doc);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief selects documents by example (not using any index)
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ByExampleQuery (v8::Arguments const& argv) {
  v8::HandleScope scope;

  // expecting example, skip, limit
  if (argv.Length() < 1) {
    TRI_V8_EXCEPTION_USAGE(scope, "BY_EXAMPLE(<example>, <skip>, <limit>)");
  }

  // extract the example
  if (! argv[0]->IsObject()) {
    TRI_V8_TYPE_ERROR(scope, "<example> must be an object");
  }


  TRI_vocbase_col_t const* col;
  col = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), TRI_GetVocBaseColType());

  if (col == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
  TRI_SHARDING_COLLECTION_NOT_YET_IMPLEMENTED(scope, col);

  V8ReadTransaction trx(col->_vocbase, col->_cid);

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  TRI_document_collection_t* document = trx.documentCollection();
  TRI_shaper_t* shaper = document->getShaper();

  v8::Handle<v8::Object> example = argv[0]->ToObject();

  // extract skip and limit
  TRI_voc_ssize_t skip;
  TRI_voc_size_t limit;
  ExtractSkipAndLimit(argv, 1, skip, limit);

  // extract sub-documents
  TRI_shape_pid_t* pids;
  TRI_shaped_json_t** values = 0;
  size_t n;
  
  if (trx.orderBarrier(trx.trxCollection()) == nullptr) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_OUT_OF_MEMORY);
  }

  v8::Handle<v8::Object> err;
  res = SetupExampleObject(example, shaper, n, pids, values, &err);

  if (res == TRI_RESULT_ELEMENT_NOT_FOUND) {
    // empty result
    return scope.Close(EmptyResult());
  }

  if (res != TRI_ERROR_NO_ERROR) {
    return scope.Close(v8::ThrowException(err));
  }

  // setup result
  v8::Handle<v8::Object> result = v8::Object::New();
  v8::Handle<v8::Array> documents = v8::Array::New();
  result->Set(v8::String::New("documents"), documents);

  // ...........................................................................
  // inside a read transaction
  // ...........................................................................

  trx.lockRead();

  // find documents by example
  vector<TRI_doc_mptr_copy_t> filtered = TRI_SelectByExample(trx.trxCollection(), n,  pids, values);

  trx.finish(res);

  // ...........................................................................
  // outside a read transaction
  // ...........................................................................

  // convert to list of shaped jsons
  size_t total = filtered.size();
  size_t count = 0;
  bool error = false;
  
  if (0 < total) {
    size_t s;
    size_t e;

    CalculateSkipLimitSlice(filtered.size(), skip, limit, s, e);

    if (s < e) {
      for (size_t j = s; j < e; ++j) {
        TRI_doc_mptr_copy_t* mptr = &filtered[j];

        v8::Handle<v8::Value> doc = WRAP_SHAPED_JSON(trx, col->_cid, mptr);

        if (doc.IsEmpty()) {
          error = true;
          break;
        }
        else {
          documents->Set((uint32_t) count++, doc);
        }
      }
    }
  }
  

  result->Set(v8::String::New("total"), v8::Integer::New((int32_t) total));
  result->Set(v8::String::New("count"), v8::Integer::New((int32_t) count));

  CleanupExampleObject(shaper->_memoryZone, n, pids, values);

  if (error) {
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief selects documents by example using a hash index
///
/// It is the callers responsibility to acquire and free the required locks
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ByExampleHashIndexQuery (V8ReadTransaction& trx,
                                                      TRI_vocbase_col_t const* collection,
                                                      v8::Handle<v8::Object>* err,
                                                      v8::Arguments const& argv) {
  v8::HandleScope scope;

  // expecting index, example, skip, and limit
  if (argv.Length() < 2) {
    TRI_V8_EXCEPTION_USAGE(scope, "EXAMPLE_HASH(<index>, <example>, <skip>, <limit>)");
  }

  // extract the example
  if (! argv[1]->IsObject()) {
    TRI_V8_TYPE_ERROR(scope, "<example> must be an object");
  }

  v8::Handle<v8::Object> example = argv[1]->ToObject();

  if (trx.orderBarrier(trx.trxCollection()) == nullptr) {
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  // extract skip and limit
  TRI_voc_ssize_t skip;
  TRI_voc_size_t limit;

  ExtractSkipAndLimit(argv, 2, skip, limit);

  // setup result
  v8::Handle<v8::Object> result = v8::Object::New();

  v8::Handle<v8::Array> documents = v8::Array::New();
  result->Set(v8::String::New("documents"), documents);

  // extract the index
  TRI_index_t* idx = TRI_LookupIndexByHandle(trx.resolver(), collection, argv[0], false, err);

  if (idx == nullptr) {
    return scope.Close(v8::ThrowException(*err));
  }

  if (idx->_type != TRI_IDX_TYPE_HASH_INDEX) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_NO_INDEX);
  }

  TRI_hash_index_t* hashIndex = (TRI_hash_index_t*) idx;

  // convert the example (index is locked by lockRead)
  TRI_index_search_value_t searchValue;
  
  TRI_document_collection_t* document = trx.documentCollection();
  TRI_shaper_t* shaper = document->getShaper();
  int res = SetupSearchValue(&hashIndex->_paths, example, shaper, searchValue, err);

  if (res != TRI_ERROR_NO_ERROR) {
    if (res == TRI_RESULT_ELEMENT_NOT_FOUND) {
      return scope.Close(EmptyResult());
    }

    return scope.Close(v8::ThrowException(*err));
  }

  // find the matches
  TRI_index_result_t list = TRI_LookupHashIndex(idx, &searchValue);
  DestroySearchValue(shaper->_memoryZone, searchValue);

  // convert result
  size_t total = list._length;
  size_t count = 0;
  bool error = false;

  if (0 < total) {
    size_t s;
    size_t e;

    CalculateSkipLimitSlice(total, skip, limit, s, e);

    if (s < e) {
      for (size_t i = s;  i < e;  ++i) {
        v8::Handle<v8::Value> doc = WRAP_SHAPED_JSON(trx, collection->_cid, list._documents[i]);

        if (doc.IsEmpty()) {
          error = true;
          break;
        }
        else {
          documents->Set((uint32_t) count++, doc);
        }
      }
    }
  }

  // free data allocated by hash index result
  TRI_DestroyIndexResult(&list);

  result->Set(v8::String::New("total"), v8::Number::New((double) total));
  result->Set(v8::String::New("count"), v8::Number::New((double) count));
        
  if (error) {
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief selects documents by example using a hash index
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ByExampleHashIndex (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_col_t const* col;
  col = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), TRI_GetVocBaseColType());

  if (col == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
  TRI_SHARDING_COLLECTION_NOT_YET_IMPLEMENTED(scope, col);

  V8ReadTransaction trx(col->_vocbase, col->_cid);

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  v8::Handle<v8::Object> err;

  // .............................................................................
  // inside a read transaction
  // .............................................................................

  trx.lockRead();

  v8::Handle<v8::Value> result = ByExampleHashIndexQuery(trx, col, &err, argv);

  trx.finish(res);

  // .............................................................................
  // outside a write transaction
  // .............................................................................

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief selects documents by condition using a skiplist index
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ByConditionSkiplist (v8::Arguments const& argv) {
  std::string const signature("BY_CONDITION_SKIPLIST(<index>, <conditions>, <skip>, <limit>)");

  return ExecuteSkiplistQuery(argv, signature, QUERY_CONDITION);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief selects documents by example using a skiplist index
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ByExampleSkiplist (v8::Arguments const& argv) {
  std::string const signature("BY_EXAMPLE_SKIPLIST(<index>, <example>, <skip>, <limit>)");

  return ExecuteSkiplistQuery(argv, signature, QUERY_EXAMPLE);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief selects documents by example using a bitarray index
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ByExampleBitarray (v8::Arguments const& argv) {
  std::string const signature("BY_EXAMPLE_BITARRAY(<index>, <example>, <skip>, <limit>)");

  return ExecuteBitarrayQuery(argv, signature, QUERY_EXAMPLE);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief selects documents by condition using a bitarray index
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ByConditionBitarray (v8::Arguments const& argv) {
  std::string const signature("BY_CONDITION_BITARRAY(<index>, <conditions>, <skip>, <limit>)");

  return ExecuteBitarrayQuery(argv, signature, QUERY_CONDITION);
}

struct collection_checksum_t {
  collection_checksum_t (CollectionNameResolver const* resolver) 
    : _resolver(resolver), 
      _checksum(0) {
  }

  CollectionNameResolver const*  _resolver;
  TRI_string_buffer_t            _buffer;
  uint32_t                       _checksum;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief callback for checksum calculation, WR = with _rid, WD = with data
////////////////////////////////////////////////////////////////////////////////

template<bool WR, bool WD> static bool ChecksumCalculator (TRI_doc_mptr_t const* mptr, 
                                                           TRI_document_collection_t* document, 
                                                           void* data) {
  // This callback is only called in TRI_DocumentIteratorDocumentCollection
  // and there we have an ongoing transaction. Therefore all master pointer
  // and data pointer accesses here are safe!
  TRI_df_marker_t const* marker = static_cast<TRI_df_marker_t const*>(mptr->getDataPtr());  // PROTECTED by trx in calling function TRI_DocumentIteratorDocumentCollection
  collection_checksum_t* helper = static_cast<collection_checksum_t*>(data);
  uint32_t localCrc;

  if (marker->_type == TRI_DOC_MARKER_KEY_DOCUMENT ||
      marker->_type == TRI_WAL_MARKER_DOCUMENT) {
    localCrc = TRI_Crc32HashString(TRI_EXTRACT_MARKER_KEY(mptr));  // PROTECTED by trx in calling function TRI_DocumentIteratorDocumentCollection
    if (WR) {
      localCrc += TRI_Crc32HashPointer(&mptr->_rid, sizeof(TRI_voc_rid_t));
    }
  }
  else if (marker->_type == TRI_DOC_MARKER_KEY_EDGE ||
           marker->_type == TRI_WAL_MARKER_EDGE) {
    // must convert _rid, _fromCid, _toCid into strings for portability
    localCrc = TRI_Crc32HashString(TRI_EXTRACT_MARKER_KEY(mptr));  // PROTECTED by trx in calling function TRI_DocumentIteratorDocumentCollection
    if (WR) {
      localCrc += TRI_Crc32HashPointer(&mptr->_rid, sizeof(TRI_voc_rid_t));
    }

    if (marker->_type == TRI_DOC_MARKER_KEY_EDGE) {
      TRI_doc_edge_key_marker_t const* e = reinterpret_cast<TRI_doc_edge_key_marker_t const*>(marker);
      string const extra = helper->_resolver->getCollectionNameCluster(e->_toCid) + TRI_DOCUMENT_HANDLE_SEPARATOR_CHR + string(((char*) marker) + e->_offsetToKey) +
                           helper->_resolver->getCollectionNameCluster(e->_fromCid) + TRI_DOCUMENT_HANDLE_SEPARATOR_CHR + string(((char*) marker) + e->_offsetFromKey); 
  
      localCrc += TRI_Crc32HashPointer(extra.c_str(), extra.size());
    }
    else {
      triagens::wal::edge_marker_t const* e = reinterpret_cast<triagens::wal::edge_marker_t const*>(marker);
      string const extra = helper->_resolver->getCollectionNameCluster(e->_toCid) + TRI_DOCUMENT_HANDLE_SEPARATOR_CHR + string(((char*) marker) + e->_offsetToKey) +
                           helper->_resolver->getCollectionNameCluster(e->_fromCid) + TRI_DOCUMENT_HANDLE_SEPARATOR_CHR + string(((char*) marker) + e->_offsetFromKey); 
  
      localCrc += TRI_Crc32HashPointer(extra.c_str(), extra.size());
    }
  }
  else {
    return true;
  }

  if (WD) {
    // with data
    void const* d = static_cast<void const*>(marker);

    TRI_shaped_json_t shaped;
    TRI_EXTRACT_SHAPED_JSON_MARKER(shaped, d);

    TRI_StringifyArrayShapedJson(document->getShaper(), &helper->_buffer, &shaped, false);
    localCrc += TRI_Crc32HashPointer(TRI_BeginStringBuffer(&helper->_buffer), TRI_LengthStringBuffer(&helper->_buffer));
    TRI_ResetStringBuffer(&helper->_buffer);
  }

  helper->_checksum += localCrc;

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief calculates a checksum for the data in a collection
/// @startDocuBlock collection_checksum
/// `collection.checksum(withRevisions, withData)`
///
/// The *checksum* operation calculates a CRC32 checksum of the keys 
/// contained in collection *collection*.
///
/// If the optional argument *withRevisions* is set to *true*, then the 
/// revision ids of the documents are also included in the checksumming.
/// 
/// If the optional argument *withData* is set to *true*, then the 
/// actual document data is also checksummed. Including the document data in
/// checksumming will make the calculation slower, but is more accurate.
///
/// Note: this method is not available in a cluster.
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ChecksumCollection (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (ServerState::instance()->isCoordinator()) {
    // renaming a collection in a cluster is unsupported
    TRI_V8_EXCEPTION(scope, TRI_ERROR_CLUSTER_UNSUPPORTED);
  }

  TRI_vocbase_col_t const* col;
  col = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), TRI_GetVocBaseColType());

  if (col == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
  TRI_SHARDING_COLLECTION_NOT_YET_IMPLEMENTED(scope, col);
  
  bool withRevisions = false;
  if (argv.Length() > 0) {
    withRevisions = TRI_ObjectToBoolean(argv[0]);
  }

  bool withData = false;
  if (argv.Length() > 1) {
    withData = TRI_ObjectToBoolean(argv[1]);
  }

  V8ReadTransaction trx(col->_vocbase, col->_cid);

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }
  
  TRI_document_collection_t* document = trx.documentCollection();

  if (trx.orderBarrier(trx.trxCollection()) == nullptr) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_OUT_OF_MEMORY);
  }
  
  collection_checksum_t helper(trx.resolver());
    
  // .............................................................................
  // inside a read transaction
  // .............................................................................

  trx.lockRead();
  // get last tick
  const string rid = StringUtils::itoa(document->_info._revision);

  if (withData) {
    TRI_InitStringBuffer(&helper._buffer, TRI_CORE_MEM_ZONE);

    if (withRevisions) {
      TRI_DocumentIteratorDocumentCollection(&trx, document, &helper, &ChecksumCalculator<true, true>);
    }
    else {
      TRI_DocumentIteratorDocumentCollection(&trx, document, &helper, &ChecksumCalculator<false, true>);
    }

    TRI_DestroyStringBuffer(&helper._buffer);
  }
  else {
    if (withRevisions) {
      TRI_DocumentIteratorDocumentCollection(&trx, document, &helper, &ChecksumCalculator<true, false>);
    }
    else {
      TRI_DocumentIteratorDocumentCollection(&trx, document, &helper, &ChecksumCalculator<false, false>);
    }
  }

  trx.finish(res);

  // .............................................................................
  // outside a write transaction
  // .............................................................................

  v8::Handle<v8::Object> result = v8::Object::New();
  result->Set(v8::String::New("checksum"), v8::Number::New(helper._checksum));
  result->Set(v8::String::New("revision"), v8::String::New(rid.c_str(), (int) rid.size()));

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief selects all edges for a set of vertices
///
/// @FUN{@FA{edge-collection}.edges(@FA{vertex})}
///
/// The @FN{edges} operator finds all edges starting from (outbound) or ending
/// in (inbound) @FA{vertex}.
///
/// @FUN{@FA{edge-collection}.edges(@FA{vertices})}
///
/// The @FN{edges} operator finds all edges starting from (outbound) or ending
/// in (inbound) a document from @FA{vertices}, which must a list of documents
/// or document handles.
///
/// @EXAMPLES
///
/// @verbinclude shell-edge-edges
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_EdgesQuery (v8::Arguments const& argv) {
  return EdgesQuery(TRI_EDGE_ANY, argv);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief selects all inbound edges
///
/// @FUN{@FA{edge-collection}.inEdges(@FA{vertex})}
///
/// The @FN{edges} operator finds all edges ending in (inbound) @FA{vertex}.
///
/// @FUN{@FA{edge-collection}.inEdges(@FA{vertices})}
///
/// The @FN{edges} operator finds all edges ending in (inbound) a document from
/// @FA{vertices}, which must a list of documents or document handles.
///
/// @EXAMPLES
///
/// @verbinclude shell-edge-in-edges
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_InEdgesQuery (v8::Arguments const& argv) {
  return EdgesQuery(TRI_EDGE_IN, argv);
}
 
////////////////////////////////////////////////////////////////////////////////
/// @brief selects the n first documents in the collection
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_FirstQuery (v8::Arguments const& argv) {
  v8::HandleScope scope;
  
  if (argv.Length() > 1) {
    TRI_V8_EXCEPTION_USAGE(scope, "FIRST(<count>)");
  }
  
  int64_t count = 1;
  bool returnList = false;

  // if argument is supplied, we'll return a list - otherwise we simply return the first doc
  if (argv.Length() == 1) {
    if (! argv[0]->IsUndefined()) {
      count = TRI_ObjectToInt64(argv[0]);
      returnList = true;
    }
  }

  if (count < 1) {
    TRI_V8_EXCEPTION_PARAMETER(scope, "invalid value for <count>");
  }

  TRI_vocbase_col_t const* col;
  col = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), TRI_GetVocBaseColType());

  if (col == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }

  SingleCollectionReadOnlyTransaction<V8TransactionContext<true>> trx(col->_vocbase, col->_cid);

  int res = trx.begin();
        
  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  std::vector<TRI_doc_mptr_copy_t> documents;
  res = trx.readPositional(documents, 0, count);
  trx.finish(res);

  size_t const n = documents.size();

  if (returnList) {
    v8::Handle<v8::Array> result = v8::Array::New((int) n);

    uint32_t j = 0;

    for (size_t i = 0; i < n; ++i) {
      v8::Handle<v8::Value> doc = WRAP_SHAPED_JSON(trx, col->_cid, &documents[i]);
        
      if (doc.IsEmpty()) {
        // error
        TRI_V8_EXCEPTION_MEMORY(scope);
      }

      result->Set(j++, doc);
    }
    
    return scope.Close(result);
  }
  else {
    if (n == 0) {
      return scope.Close(v8::Null());
    }

    v8::Handle<v8::Value> result = WRAP_SHAPED_JSON(trx, col->_cid, &documents[0]);

    if (result.IsEmpty()) {
      TRI_V8_EXCEPTION_MEMORY(scope);
    }
   
    return scope.Close(result);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief queries the fulltext index
///
/// the caller must ensure all relevant locks are acquired and freed
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> FulltextQuery (V8ReadTransaction& trx,
                                            TRI_vocbase_col_t const* collection,
                                            v8::Handle<v8::Object>* err,
                                            v8::Arguments const& argv) {
  v8::HandleScope scope;

  // expect: FULLTEXT(<index-handle>, <query>)
  if (argv.Length() != 2) {
    TRI_V8_EXCEPTION_USAGE(scope, "FULLTEXT(<index-handle>, <query>)");
  }

  // extract the index
  TRI_index_t* idx = TRI_LookupIndexByHandle(trx.resolver(), collection, argv[0], false, err);

  if (idx == nullptr) {
    return scope.Close(v8::ThrowException(*err));
  }

  if (idx->_type != TRI_IDX_TYPE_FULLTEXT_INDEX) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_NO_INDEX);
  }

  const string queryString = TRI_ObjectToString(argv[1]);
  bool isSubstringQuery = false;

  TRI_fulltext_query_t* query = TRI_CreateQueryFulltextIndex(TRI_FULLTEXT_SEARCH_MAX_WORDS);

  if (! query) {
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  int res = TRI_ParseQueryFulltextIndex(query, queryString.c_str(), &isSubstringQuery);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_FreeQueryFulltextIndex(query);

    TRI_V8_EXCEPTION(scope, res);
  }

  TRI_fulltext_index_t* fulltextIndex = (TRI_fulltext_index_t*) idx;

  if (isSubstringQuery && ! fulltextIndex->_indexSubstrings) {
    TRI_FreeQueryFulltextIndex(query);

    TRI_V8_EXCEPTION(scope, TRI_ERROR_NOT_IMPLEMENTED);
  }

  TRI_fulltext_result_t* queryResult = TRI_QueryFulltextIndex(fulltextIndex->_fulltextIndex, query);

  if (! queryResult) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "internal error in fulltext index query");
  }

  if (trx.orderBarrier(trx.trxCollection()) == nullptr) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_OUT_OF_MEMORY);
  }

  // setup result
  v8::Handle<v8::Object> result = v8::Object::New();

  v8::Handle<v8::Array> documents = v8::Array::New();
  result->Set(v8::String::New("documents"), documents);

  bool error = false;

  for (uint32_t i = 0; i < queryResult->_numDocuments; ++i) {
    v8::Handle<v8::Value> doc = WRAP_SHAPED_JSON(trx, collection->_cid, (TRI_doc_mptr_t const*) queryResult->_documents[i]);

    if (doc.IsEmpty()) {
      error = true;
      break;
    }

    documents->Set(i, doc);
  }

  TRI_FreeResultFulltextIndex(queryResult);
    
  if (error) {
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief queries the fulltext index
///
/// @FUN{@FA{collection}.FULLTEXT(@FA{index-handle}, @FA{query})}
///
/// The @FN{FULLTEXT} operator performs a fulltext search using the specified
/// index and the specified @FA{query}.
///
/// @FA{query} must contain a comma-separated list of words to look for.
/// Each word can optionally be prefixed with one of the following command
/// literals:
/// - @LIT{prefix}: perform a prefix-search for the word following
/// - @LIT{substring}: perform substring-matching for the word following. This
///   option is only supported for fulltext indexes that have been created with
///   the @LIT{indexSubstrings} option
/// - @LIT{complete}: only match the complete following word (this is the default)
///
/// @EXAMPLES
///
/// @verbinclude shell-simple-fulltext
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_FulltextQuery (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_col_t const* col;
  col = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), TRI_GetVocBaseColType());

  if (col == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
  TRI_SHARDING_COLLECTION_NOT_YET_IMPLEMENTED(scope, col);

  V8ReadTransaction trx(col->_vocbase, col->_cid);

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  v8::Handle<v8::Object> err;

  // .............................................................................
  // inside a read transaction
  // .............................................................................

  trx.lockRead();

  v8::Handle<v8::Value> result = FulltextQuery(trx, col, &err, argv);

  trx.finish(res);

  // .............................................................................
  // outside a write transaction
  // .............................................................................

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief selects the n last documents in the collection
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_LastQuery (v8::Arguments const& argv) {
  v8::HandleScope scope;
  
  if (argv.Length() > 1) {
    TRI_V8_EXCEPTION_USAGE(scope, "LAST(<count>)");
  }

  int64_t count = 1;
  bool returnList = false;

  // if argument is supplied, we'll return a list - otherwise we simply return the last doc
  if (argv.Length() == 1) {
    if (! argv[0]->IsUndefined()) {
      count = TRI_ObjectToInt64(argv[0]);
      returnList = true;
    }
  }

  if (count < 1) {
    TRI_V8_EXCEPTION_PARAMETER(scope, "invalid value for <count>");
  }

  TRI_vocbase_col_t const* col;
  col = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), TRI_GetVocBaseColType());

  if (col == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
  TRI_SHARDING_COLLECTION_NOT_YET_IMPLEMENTED(scope, col);

  SingleCollectionReadOnlyTransaction<V8TransactionContext<true>> trx(col->_vocbase, col->_cid);

  int res = trx.begin();
        
  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  vector<TRI_doc_mptr_copy_t> documents;
  res = trx.readPositional(documents, -1, count);
  trx.finish(res);

  size_t const n = documents.size();

  if (returnList) {
    v8::Handle<v8::Array> result = v8::Array::New((int) n);

    uint32_t j = 0;

    for (size_t i = 0; i < n; ++i) {
      v8::Handle<v8::Value> doc = WRAP_SHAPED_JSON(trx, col->_cid, &documents[i]);
        
      if (doc.IsEmpty()) {
        // error
        TRI_V8_EXCEPTION_MEMORY(scope);
      }

      result->Set(j++, doc);
    }
    
    return scope.Close(result);
  }
  else {
    if (n == 0) {
      return scope.Close(v8::Null());
    }

    v8::Handle<v8::Value> result = WRAP_SHAPED_JSON(trx, col->_cid, &documents[0]);

    if (result.IsEmpty()) {
      TRI_V8_EXCEPTION_MEMORY(scope);
    }
   
    return scope.Close(result);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief selects points near a given coordinate
///
/// the caller must ensure all relevant locks are acquired and freed
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> NearQuery (V8ReadTransaction& trx,
                                        TRI_vocbase_col_t const* collection,
                                        v8::Handle<v8::Object>* err,
                                        v8::Arguments const& argv) {
  v8::HandleScope scope;

  // expect: NEAR(<index-id>, <latitude>, <longitude>, <limit>)
  if (argv.Length() != 4) {
    TRI_V8_EXCEPTION_USAGE(scope, "NEAR(<index-handle>, <latitude>, <longitude>, <limit>)");
  }

  // extract the index
  TRI_index_t* idx = TRI_LookupIndexByHandle(trx.resolver(), collection, argv[0], false, err);

  if (idx == nullptr) {
    return scope.Close(v8::ThrowException(*err));
  }

  if (idx->_type != TRI_IDX_TYPE_GEO1_INDEX && 
      idx->_type != TRI_IDX_TYPE_GEO2_INDEX) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_NO_INDEX);
  }

  // extract latitude and longitude
  double latitude = TRI_ObjectToDouble(argv[1]);
  double longitude = TRI_ObjectToDouble(argv[2]);

  // extract the limit
  TRI_voc_ssize_t limit = (TRI_voc_ssize_t) TRI_ObjectToDouble(argv[3]);

  // setup result
  v8::Handle<v8::Object> result = v8::Object::New();

  v8::Handle<v8::Array> documents = v8::Array::New();
  result->Set(v8::String::New("documents"), documents);

  v8::Handle<v8::Array> distances = v8::Array::New();
  result->Set(v8::String::New("distances"), distances);

  GeoCoordinates* cors = TRI_NearestGeoIndex(idx, latitude, longitude, limit);

  if (cors != 0) {
    int res = StoreGeoResult(trx, collection, cors, documents, distances);

    if (res != TRI_ERROR_NO_ERROR) {
      TRI_V8_EXCEPTION(scope, res);
    }
  }

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief selects points near a given coordinate
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_NearQuery (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_col_t const* col;
  col = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), TRI_GetVocBaseColType());

  if (col == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
  TRI_SHARDING_COLLECTION_NOT_YET_IMPLEMENTED(scope, col);

  V8ReadTransaction trx(col->_vocbase, col->_cid);

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  v8::Handle<v8::Object> err;

  // .............................................................................
  // inside a read transaction
  // .............................................................................

  trx.lockRead();

  v8::Handle<v8::Value> result = NearQuery(trx, col, &err, argv);

  trx.finish(res);

  // .............................................................................
  // outside a write transaction
  // .............................................................................

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief selects all outbound edges
///
/// @FUN{@FA{edge-collection}.outEdges(@FA{vertex})}
///
/// The @FN{edges} operator finds all edges starting from (outbound)
/// @FA{vertices}.
///
/// @FUN{@FA{edge-collection}.outEdges(@FA{vertices})}
///
/// The @FN{edges} operator finds all edges starting from (outbound) a document
/// from @FA{vertices}, which must a list of documents or document handles.
///
/// @EXAMPLES
///
/// @verbinclude shell-edge-out-edges
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_OutEdgesQuery (v8::Arguments const& argv) {
  return EdgesQuery(TRI_EDGE_OUT, argv);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief selects points within a given radius
///
/// the caller must ensure all relevant locks are acquired and freed
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> WithinQuery (V8ReadTransaction& trx,
                                          TRI_vocbase_col_t const* collection,
                                          v8::Handle<v8::Object>* err,
                                          v8::Arguments const& argv) {
  v8::HandleScope scope;

  // expect: WITHIN(<index-handle>, <latitude>, <longitude>, <radius>)
  if (argv.Length() != 4) {
    TRI_V8_EXCEPTION_USAGE(scope, "WITHIN(<index-handle>, <latitude>, <longitude>, <radius>)");
  }

  // extract the index
  TRI_index_t* idx = TRI_LookupIndexByHandle(trx.resolver(), collection, argv[0], false, err);

  if (idx == nullptr) {
    return scope.Close(v8::ThrowException(*err));
  }

  if (idx->_type != TRI_IDX_TYPE_GEO1_INDEX && 
      idx->_type != TRI_IDX_TYPE_GEO2_INDEX) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_NO_INDEX);
  }

  // extract latitude and longitude
  double latitude = TRI_ObjectToDouble(argv[1]);
  double longitude = TRI_ObjectToDouble(argv[2]);

  // extract the radius
  double radius = TRI_ObjectToDouble(argv[3]);

  // setup result
  v8::Handle<v8::Object> result = v8::Object::New();

  v8::Handle<v8::Array> documents = v8::Array::New();
  result->Set(v8::String::New("documents"), documents);

  v8::Handle<v8::Array> distances = v8::Array::New();
  result->Set(v8::String::New("distances"), distances);

  GeoCoordinates* cors = TRI_WithinGeoIndex(idx, latitude, longitude, radius);

  if (cors != 0) {
    int res = StoreGeoResult(trx, collection, cors, documents, distances);

    if (res != TRI_ERROR_NO_ERROR) {
      TRI_V8_EXCEPTION(scope, res);
    }
  }

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief selects points within a given radius
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_WithinQuery (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_col_t const* col;
  col = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), TRI_GetVocBaseColType());

  if (col == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
  TRI_SHARDING_COLLECTION_NOT_YET_IMPLEMENTED(scope, col);

  V8ReadTransaction trx(col->_vocbase, col->_cid);

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  v8::Handle<v8::Object> err;

  // .............................................................................
  // inside a read transaction
  // .............................................................................

  trx.lockRead();

  v8::Handle<v8::Value> result = WithinQuery(trx, col, &err, argv);

  trx.finish(res);

  // .............................................................................
  // outside a write transaction
  // .............................................................................

  return scope.Close(result);
}

// -----------------------------------------------------------------------------
// --SECTION--                                                            MODULE
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief creates the query functions
////////////////////////////////////////////////////////////////////////////////

void TRI_InitV8Queries (v8::Handle<v8::Context> context) {
  v8::HandleScope scope;

  v8::Handle<v8::ObjectTemplate> rt;

  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  TRI_v8_global_t* v8g = (TRI_v8_global_t*) isolate->GetData();

  TRI_ASSERT(v8g != 0);

  // .............................................................................
  // generate the TRI_vocbase_col_t template
  // .............................................................................

  rt = v8g->VocbaseColTempl;

  TRI_AddMethodVocbase(rt, "ALL", JS_AllQuery, true);
  TRI_AddMethodVocbase(rt, "ANY", JS_AnyQuery, true);
  TRI_AddMethodVocbase(rt, "BY_CONDITION_BITARRAY", JS_ByConditionBitarray, true);
  TRI_AddMethodVocbase(rt, "BY_CONDITION_SKIPLIST", JS_ByConditionSkiplist, true);
  TRI_AddMethodVocbase(rt, "BY_EXAMPLE", JS_ByExampleQuery, true);
  TRI_AddMethodVocbase(rt, "BY_EXAMPLE_BITARRAY", JS_ByExampleBitarray, true);
  TRI_AddMethodVocbase(rt, "BY_EXAMPLE_HASH", JS_ByExampleHashIndex, true);
  TRI_AddMethodVocbase(rt, "BY_EXAMPLE_SKIPLIST", JS_ByExampleSkiplist, true);
  TRI_AddMethodVocbase(rt, "checksum", JS_ChecksumCollection);
  TRI_AddMethodVocbase(rt, "EDGES", JS_EdgesQuery, true);
  TRI_AddMethodVocbase(rt, "FIRST", JS_FirstQuery, true);
  TRI_AddMethodVocbase(rt, "FULLTEXT", JS_FulltextQuery, true);
  TRI_AddMethodVocbase(rt, "INEDGES", JS_InEdgesQuery, true);
  TRI_AddMethodVocbase(rt, "LAST", JS_LastQuery, true);
  TRI_AddMethodVocbase(rt, "NEAR", JS_NearQuery, true);

  // internal method. not intended to be used by end-users
  TRI_AddMethodVocbase(rt, "OFFSET", JS_OffsetQuery, true); 

  TRI_AddMethodVocbase(rt, "OUTEDGES", JS_OutEdgesQuery, true);
  TRI_AddMethodVocbase(rt, "WITHIN", JS_WithinQuery);
}

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
