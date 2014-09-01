/*jslint indent: 2, nomen: true, maxlen: 120, sloppy: true, vars: true, white: true, plusplus: true */
/*global require, exports*/

////////////////////////////////////////////////////////////////////////////////
/// @brief Pregel wrapper for vertices. Will add content of the vertex and all 
///   functions allowed within a pregel execution step.
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
/// @author Florian Bartels, Michael Hackstein, Heiko Kernbach
/// @author Copyright 2011-2014, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////
var p = require("org/arangodb/profiler");

var internal = require("internal");
var db = require("internal").db;
var pregel = require("org/arangodb/pregel");
var _ = require("underscore");

var Vertex = function (jsonData, mapping, parent) {
  var t = p.stopWatch();
  var Edge = pregel.Edge;
  var self = this;
  _.each(jsonData, function(val, key) {
    self[key] = val;
  });
  var shard = this._locationInfo.shard;
  this.__resultShard = db[mapping.getResultShard(shard)];
  this.__parent = parent;
  this.__active = true;
  this.__inactiveSince = 0;
  this.__deleted = false;
  this.__result = {};
  var respEdges = mapping.getResponsibleEdgeShards(shard);
  this._outEdges = [];
  _.each(respEdges, function(edgeShard) {
    var outEdges = db[edgeShard].outEdges(self._id);
    _.each(outEdges, function (json) {
      var e = new Edge(json, mapping, edgeShard);
      self._outEdges.push(e);
    });
  });
  p.storeWatch("ConstructVertex", t);
};


Vertex.prototype._incrInactiveSince = function () {
  if (this.__inactiveSince > 3) {
    this.__inactiveSince = -1;
    this._save(true);
    this.__result = {};
  } else {
    this.__inactiveSince++;
  }
};

Vertex.prototype._deactivate = function () {
  if (this._isActive()) {
    this.__parent.__actives--;
  }
  this.__active = false;
};

Vertex.prototype._activate = function () {
  if (this._isDeleted()) {
    return;
  }
  if (!this._isActive()) {
    this.__parent.__actives++;
  }
  this.__active = true;
  if (this.__inactiveSince === -1) {
    this.__inactiveSince = 0;
    this.__result = this.__resultShard.document(this._key).result;
  }
};

Vertex.prototype._isActive = function () {
  return this.__active && !this.__deleted;
};

Vertex.prototype._isDeleted = function () {
  return this.__deleted;
};

Vertex.prototype._delete = function () {
  if (this._isActive()) {
    this.__parent.__actives--;
  }
  this._save();
  this.__deleted = true;
  delete this.__parent[this._id];
};

Vertex.prototype._getResult = function () {
  return this.__result;
};

Vertex.prototype._setResult = function (result) {
  this.__result = result;
};

Vertex.prototype._save = function (dontSaveEdges) {
  var t = p.stopWatch();
  if (dontSaveEdges) {
    this._outEdges.forEach(function(e) {
      e._save();
    });
  }
  this.__resultShard.save({
    _key: this._key,
    _deleted : this.__deleted,
    result: this._getResult()
  });
  p.storeWatch("SaveVertex", t);
};


exports.Vertex = Vertex;
