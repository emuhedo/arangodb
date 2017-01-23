////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2016 ArangoDB GmbH, Cologne, Germany
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
/// @author Simon Grätzer
////////////////////////////////////////////////////////////////////////////////

#include "GraphStore.h"

#include <algorithm>
#include "Basics/Exceptions.h"
#include "Cluster/ClusterInfo.h"
#include "Indexes/EdgeIndex.h"
#include "Indexes/Index.h"
#include "Pregel/WorkerConfig.h"
#include "Utils.h"
#include "Utils/CollectionNameResolver.h"
#include "Utils/ExplicitTransaction.h"
#include "Utils/OperationCursor.h"
#include "Utils/StandaloneTransactionContext.h"
#include "Utils/Transaction.h"
#include "VocBase/EdgeCollectionInfo.h"
#include "VocBase/LogicalCollection.h"
#include "VocBase/ticks.h"
#include "VocBase/vocbase.h"

using namespace arangodb;
using namespace arangodb::pregel;

template <typename V, typename E>
GraphStore<V, E>::GraphStore(TRI_vocbase_t* vb, GraphFormat* graphFormat)
    : _vocbaseGuard(vb), _graphFormat(graphFormat) {}

template <typename V, typename E>
GraphStore<V, E>::~GraphStore() {
  _destroyed = true;
  cleanupTransactions();
}

template <typename V, typename E>
void GraphStore<V, E>::loadShards(WorkerConfig const& state) {
  _createReadTransaction(state);

  std::map<CollectionID, std::vector<ShardID>> const& vertexMap =
      state.vertexCollectionShards();
  std::map<CollectionID, std::vector<ShardID>> const& edgeMap =
      state.edgeCollectionShards();
  for (auto const& pair : vertexMap) {
    std::vector<ShardID> const& vertexShards = pair.second;
    for (size_t i = 0; i < vertexShards.size(); i++) {
      // we might have already loaded these shards
      if (_loadedShards.find(vertexShards[i]) != _loadedShards.end()) {
        continue;
      }

      // distributeshardslike should cause the edges for a vertex to be
      // in the same shard index. x in vertexShard2 => E(x) in edgeShard2
      for (auto const& pair2 : edgeMap) {
        std::vector<ShardID> const& edgeShards = pair2.second;

        if (vertexShards.size() != edgeShards.size()) {
          THROW_ARANGO_EXCEPTION_MESSAGE(
              TRI_ERROR_BAD_PARAMETER,
              "Collections need to have the same number of shards");
        }
        _loadVertices(state, vertexShards[i], edgeShards[i]);
        _loadedShards.insert(vertexShards[i]);
      }
    }
  }
  cleanupTransactions();
}

template <typename V, typename E>
void GraphStore<V, E>::loadDocument(WorkerConfig const& config,
                                    std::string const& documentID) {
  
  PregelID _id = config.documentIdToPregel(documentID);
  if (config.isLocalVertexShard(_id.shard)) {
    loadDocument(config, _id.shard, _id.key);
  }
}

template <typename V, typename E>
void GraphStore<V, E>::loadDocument(WorkerConfig const& config,
                                    prgl_shard_t sourceShard,
                                    std::string const& _key) {
  if (_readTrx == nullptr) {
    _createReadTransaction(config);
  }

  ShardID const& vertexShard = config.globalShardIDs()[sourceShard];

  VPackBuilder builder;
  builder.openObject();
  builder.add(StaticStrings::KeyString, VPackValue(_key));
  builder.close();

  OperationOptions options;
  options.ignoreRevs = false;

  TRI_voc_cid_t cid = _readTrx->addCollectionAtRuntime(vertexShard);
  _readTrx->orderDitch(cid);  // will throw when it fails
  OperationResult opResult =
      _readTrx->document(vertexShard, builder.slice(), options);
  if (!opResult.successful()) {
    cleanupTransactions();
    THROW_ARANGO_EXCEPTION(opResult.code);
  }

  std::string documentId = _readTrx->extractIdString(opResult.slice());
  VertexEntry entry(sourceShard, _key);
  V vertexData;
  size_t size = _graphFormat->copyVertexData(entry, documentId,
                                             opResult.slice(),
                                             &vertexData, sizeof(V));
  if (size > 0) {
    entry._vertexDataOffset = _vertexData.size();
    _vertexData.push_back(vertexData);
  }
  std::map<CollectionID, std::vector<ShardID>> const& vertexMap =
      config.vertexCollectionShards();
  std::map<CollectionID, std::vector<ShardID>> const& edgeMap =
      config.edgeCollectionShards();
  for (auto const& pair : vertexMap) {
    std::vector<ShardID> const& vertexShards = pair.second;
    auto it = std::find(vertexShards.begin(), vertexShards.end(), vertexShard);
    if (it != vertexShards.end()) {
      size_t pos = it - vertexShards.begin();

      for (auto const& pair2 : edgeMap) {
        std::vector<ShardID> const& edgeShards = pair2.second;
        _loadEdges(config, edgeShards[pos], entry, documentId);
      }
    }
  }
  _index.push_back(entry);
  _localVerticeCount++;
}

template <typename V, typename E>
RangeIterator<VertexEntry> GraphStore<V, E>::vertexIterator() {
  return vertexIterator(0, _index.size());
}

template <typename V, typename E>
RangeIterator<VertexEntry> GraphStore<V, E>::vertexIterator(size_t start,
                                                            size_t end) {
  return RangeIterator<VertexEntry>(_index, start, end);
}

template <typename V, typename E>
void* GraphStore<V, E>::mutableVertexData(VertexEntry const* entry) {
  return _vertexData.data() + entry->_vertexDataOffset;
}

template <typename V, typename E>
void GraphStore<V, E>::replaceVertexData(VertexEntry const* entry, void* data,
                                         size_t size) {
  //if (size <= entry->_vertexDataOffset)
  void* ptr = _vertexData.data() + entry->_vertexDataOffset;
  memcpy(ptr, data, size);
  LOG(WARN) << "Don't use this function with varying sizes";
}

template <typename V, typename E>
RangeIterator<Edge<E>> GraphStore<V, E>::edgeIterator(
    VertexEntry const* entry) {
  size_t end = entry->_edgeDataOffset + entry->_edgeCount;
  return RangeIterator<Edge<E>>(_edges, entry->_edgeDataOffset, end);
}

template <typename V, typename E>
void GraphStore<V, E>::_createReadTransaction(WorkerConfig const& state) {
  std::vector<std::string> readColls, writeColls;
  for (auto shard : state.localVertexShardIDs()) {
    readColls.push_back(shard);
  }
  for (auto shard : state.localEdgeShardIDs()) {
    readColls.push_back(shard);
  }
  double lockTimeout =
      (double)(TRI_TRANSACTION_DEFAULT_LOCK_TIMEOUT / 1000000ULL);
  _readTrx = new ExplicitTransaction(
      StandaloneTransactionContext::Create(_vocbaseGuard.vocbase()), readColls,
      writeColls, lockTimeout, false, false);
  int res = _readTrx->begin();
  if (res != TRI_ERROR_NO_ERROR) {
    THROW_ARANGO_EXCEPTION(res);
  }
}

template <typename V, typename E>
void GraphStore<V, E>::cleanupTransactions() {
  if (_readTrx) {
    if (_readTrx->getStatus() == TRI_TRANSACTION_RUNNING) {
      if (_readTrx->commit() != TRI_ERROR_NO_ERROR) {
        LOG(WARN) << "Pregel worker: Failed to commit on a read transaction";
      }
    }
    delete _readTrx;
    _readTrx = nullptr;
  }
}

template <typename V, typename E>
void GraphStore<V, E>::_loadVertices(WorkerConfig const& state,
                                     ShardID const& vertexShard,
                                     ShardID const& edgeShard) {
  //_graphFormat->willUseCollection(vocbase, vertexShard, false);
  TRI_voc_cid_t cid = _readTrx->addCollectionAtRuntime(vertexShard);
  _readTrx->orderDitch(cid);  // will throw when it fails
  prgl_shard_t sourceShard = (prgl_shard_t)state.shardId(vertexShard);

  /*int res = _readTrx->lockRead();
  if (res != TRI_ERROR_NO_ERROR) {
    THROW_ARANGO_EXCEPTION_FORMAT(res, "while looking up vertices '%s'",
                                  vertexShard.c_str());
  }*/

  ManagedDocumentResult mmdr(_readTrx);
  std::unique_ptr<OperationCursor> cursor = _readTrx->indexScan(
      vertexShard, Transaction::CursorType::ALL, Transaction::IndexHandle(), {},
      &mmdr, 0, UINT64_MAX, 1000, false);

  if (cursor->failed()) {
    THROW_ARANGO_EXCEPTION_FORMAT(cursor->code, "while looking up shard '%s'",
                                  vertexShard.c_str());
  }
  LogicalCollection* collection = cursor->collection();
  uint64_t number = collection->numberDocuments();
  _graphFormat->willLoadVertices(number);
  
  // reserve some space for our data
  if (_index.capacity() < _index.size() + number) {
    _index.reserve(_index.size() + number);
  }
  if (_graphFormat->estimatedVertexSize() > 0) {
    if (_vertexData.capacity() < _vertexData.size() + number) {
      _vertexData.reserve(_vertexData.size() + number);
    }
  }
  // assuming more edges than nodes
  if (_edges.capacity() < _edges.size() + number) {
    _edges.reserve(_edges.size() + number);
  }
  
  std::vector<IndexLookupResult> result;
  result.reserve(1000);
  while (cursor->hasMore()) {
    cursor->getMoreMptr(result, 1000);
    for (auto const& element : result) {
      TRI_voc_rid_t revisionId = element.revisionId();
      if (collection->readRevision(_readTrx, mmdr, revisionId)) {
        VPackSlice document(mmdr.vpack());
        if (document.isExternal()) {
          document = document.resolveExternal();
        }

        // LOG(INFO) << "Loaded Vertex: " << document.toJson();
        std::string key = document.get(StaticStrings::KeyString).copyString();
        std::string documentId = _readTrx->extractIdString(document);

        VertexEntry entry(sourceShard, key);
        V vertexData;
        size_t size = _graphFormat->copyVertexData(entry, documentId, document,
                                                   &vertexData, sizeof(V));
        if (size > 0) {
          entry._vertexDataOffset = _vertexData.size();
          _vertexData.push_back(vertexData);
        }

        _loadEdges(state, edgeShard, entry, documentId);
        _index.push_back(entry);
        _localVerticeCount++;
      }
    }
  }
}

template <typename V, typename E>
void GraphStore<V, E>::_loadEdges(WorkerConfig const& state,
                                  ShardID const& edgeShard,
                                  VertexEntry& vertexEntry,
                                  std::string const& documentID) {
  
  traverser::EdgeCollectionInfo info(_readTrx, edgeShard, TRI_EDGE_OUT,
                                     StaticStrings::FromString, 0);

  ManagedDocumentResult mmdr(_readTrx);
  auto cursor = info.getEdges(documentID, &mmdr);
  if (cursor->failed()) {
    THROW_ARANGO_EXCEPTION_FORMAT(cursor->code,
                                  "while looking up edges '%s' from %s",
                                  documentID.c_str(), edgeShard.c_str());
  }

  LogicalCollection* collection = cursor->collection();
  std::vector<IndexLookupResult> result;
  result.reserve(1000);

  while (cursor->hasMore()) {
    if (vertexEntry._edgeCount == 0) {// initalize the start
      vertexEntry._edgeDataOffset = _edges.size();
    }

    cursor->getMoreMptr(result, 1000);
    for (auto const& element : result) {
      TRI_voc_rid_t revisionId = element.revisionId();
      if (collection->readRevision(_readTrx, mmdr, revisionId)) {
        VPackSlice document(mmdr.vpack());
        if (document.isExternal()) {
          document = document.resolveExternal();
        }

        // ====== actual loading ======
        vertexEntry._edgeCount += 1;

        // LOG(INFO) << "Loaded Edge: " << document.toJson();
        std::string toValue =
            document.get(StaticStrings::ToString).copyString();

        std::size_t pos = toValue.find('/');
        std::string collectionName = toValue.substr(0, pos);
        std::string _key = toValue.substr(pos + 1, toValue.length() - pos - 1);

        auto collInfo = Utils::resolveCollection(
            state.database(), collectionName, state.collectionPlanIdMap());

        // resolve the shard of the target vertex.
        ShardID responsibleShard;
        Utils::resolveShard(collInfo.get(), StaticStrings::KeyString, _key,
                            responsibleShard);
        prgl_shard_t source = (prgl_shard_t)state.shardId(edgeShard);
        prgl_shard_t target = (prgl_shard_t)state.shardId(responsibleShard);
        if (source == (prgl_shard_t)-1 || target == (prgl_shard_t)-1) {
          LOG(ERR) << "Unknown shard";
          continue;
        }

        Edge<E> edge(source, target, _key);
        // size_t size =
        _graphFormat->copyEdgeData(document, edge.data(), sizeof(E));
        // TODO store size value at some point
        _edges.push_back(edge);
        _localEdgeCount++;
      }
    }
  }

  /*res = trx.finish(res);
  if (res != TRI_ERROR_NO_ERROR) {
    THROW_ARANGO_EXCEPTION_FORMAT(res, "while looking up edges from '%s'",
                                  edgeShard.c_str());
  }*/
}

template <typename V, typename E>
void GraphStore<V, E>::storeResults(WorkerConfig const& state) {
  double start = TRI_microtime();
  
  std::vector<std::string> readColls, writeColls;
  for (auto shard : state.localVertexShardIDs()) {
    writeColls.push_back(shard);
  }
  for (auto shard : state.localEdgeShardIDs()) {
    writeColls.push_back(shard);
  }
  // for (auto shard : state.localEdgeShardIDs() ) {
  //  writeColls.push_back(shard);
  //}
  double lockTimeout =
      (double)(TRI_TRANSACTION_DEFAULT_LOCK_TIMEOUT / 1000000ULL);
  ExplicitTransaction writeTrx(
      StandaloneTransactionContext::Create(_vocbaseGuard.vocbase()), readColls,
      writeColls, lockTimeout, false, false);
  int res = writeTrx.begin();
  if (res != TRI_ERROR_NO_ERROR) {
    THROW_ARANGO_EXCEPTION(res);
  }
  OperationOptions options;
  
  auto it = _index.begin();
  while (it != _index.end()) {
    TransactionBuilderLeaser b(&writeTrx);
    
    prgl_shard_t shardID = it->shard();
    b->openArray();
    size_t buffer = 1000;
    while (it != _index.end() && buffer > 0) {
      if (it->shard() != shardID) {
        break;
      }
      
      void* data = _vertexData.data() + it->_vertexDataOffset;
      b->openObject();
      b->add(StaticStrings::KeyString, VPackValue(it->key()));
      //bool store =
      _graphFormat->buildVertexDocument(*(b.get()), data, sizeof(V));
      b->close();
      
      ++it;
      buffer--;
    }
    b->close();
    
    ShardID const& shard = state.globalShardIDs()[shardID];
    OperationResult result = writeTrx.update(shard, b->slice(), options);
    if (result.code != TRI_ERROR_NO_ERROR) {
      THROW_ARANGO_EXCEPTION(result.code);
    }
    
    // TODO loop over edges
  }
  res = writeTrx.finish(res);
  if (res != TRI_ERROR_NO_ERROR) {
    THROW_ARANGO_EXCEPTION(res);
  }
  
  LOG(INFO) << "Storing data took " << (TRI_microtime() - start) << "s";
}

template class arangodb::pregel::GraphStore<int64_t, int64_t>;
template class arangodb::pregel::GraphStore<float, float>;
template class arangodb::pregel::GraphStore<double, float>;
template class arangodb::pregel::GraphStore<double, double>;

