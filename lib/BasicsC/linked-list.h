////////////////////////////////////////////////////////////////////////////////
/// @brief linked list implementation
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
/// @author Dr. Frank Celler
/// @author Copyright 2014, ArangoDB GmbH, Cologne, Germany
/// @author Copyright 2012-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGODB_BASICS_C_LINKED__LIST_H
#define ARANGODB_BASICS_C_LINKED__LIST_H 1

#include "BasicsC/common.h"

#include "BasicsC/associative.h"

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// --SECTION--                                                       LINKED LIST
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                      public types
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief linked list entry
////////////////////////////////////////////////////////////////////////////////

typedef struct TRI_linked_list_entry_s {
  void const* _data;
  struct TRI_linked_list_entry_s* _prev;
  struct TRI_linked_list_entry_s* _next;
}
TRI_linked_list_entry_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief linked list
////////////////////////////////////////////////////////////////////////////////

typedef struct TRI_linked_list_s {
  TRI_memory_zone_t* _memoryZone;
  TRI_linked_list_entry_t* _begin;
  TRI_linked_list_entry_t* _end;
}
TRI_linked_list_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief linked array
////////////////////////////////////////////////////////////////////////////////

typedef struct TRI_linked_array_s {
  TRI_memory_zone_t* _memoryZone;
  TRI_linked_list_t _list;
  TRI_associative_pointer_t _array;
}
TRI_linked_array_t;

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief inits a linked list
////////////////////////////////////////////////////////////////////////////////

void TRI_InitLinkedList (TRI_linked_list_t*, TRI_memory_zone_t*);

////////////////////////////////////////////////////////////////////////////////
/// @brief inits a linked array
////////////////////////////////////////////////////////////////////////////////

void TRI_InitLinkedArray (TRI_linked_array_t*, TRI_memory_zone_t*);

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys a linked list, but does not free the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyLinkedList (TRI_linked_list_t*, TRI_memory_zone_t*);

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys a linked list and frees the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeLinkedList (TRI_memory_zone_t*, TRI_linked_list_t*);

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys a linked array, but does not free the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyLinkedArray (TRI_linked_array_t*);

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys a linked list and frees the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeLinkedArray (TRI_memory_zone_t*, TRI_linked_array_t*);

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief inserts an entry at the end of a linked list
////////////////////////////////////////////////////////////////////////////////

void TRI_AddLinkedList (TRI_linked_list_t*, TRI_linked_list_entry_t*);

////////////////////////////////////////////////////////////////////////////////
/// @brief inserts an entry at the beginning of a linked list
////////////////////////////////////////////////////////////////////////////////

void TRI_AddFrontLinkedList (TRI_linked_list_t*, TRI_linked_list_entry_t*);

////////////////////////////////////////////////////////////////////////////////
/// @brief removes an entry from a linked list
////////////////////////////////////////////////////////////////////////////////

void TRI_RemoveLinkedList (TRI_linked_list_t*, TRI_linked_list_entry_t*);

////////////////////////////////////////////////////////////////////////////////
/// @brief inserts an entry at the end of a linked array
////////////////////////////////////////////////////////////////////////////////

int TRI_AddLinkedArray (TRI_linked_array_t*, void const* data);

////////////////////////////////////////////////////////////////////////////////
/// @brief inserts an entry at the beginning of a linked array
////////////////////////////////////////////////////////////////////////////////

int TRI_AddFrontLinkedArray (TRI_linked_array_t*, void const* data);

////////////////////////////////////////////////////////////////////////////////
/// @brief removes an entry from a linked array
////////////////////////////////////////////////////////////////////////////////

void TRI_RemoveLinkedArray (TRI_linked_array_t*, void const* data);

////////////////////////////////////////////////////////////////////////////////
/// @brief moves an entry to the end of a linked array
////////////////////////////////////////////////////////////////////////////////

void TRI_MoveToBackLinkedArray (TRI_linked_array_t* array, void const* data);

////////////////////////////////////////////////////////////////////////////////
/// @brief pops front entry from list
////////////////////////////////////////////////////////////////////////////////

void const* TRI_PopFrontLinkedArray (TRI_linked_array_t* array);

#ifdef __cplusplus
}
#endif

#endif

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
