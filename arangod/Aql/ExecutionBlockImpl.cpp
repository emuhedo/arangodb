////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2018 ArangoDB GmbH, Cologne, Germany
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
/// @author Tobias Goedderz
/// @author Michael Hackstein
/// @author Heiko Kernbach
/// @author Jan Christoph Uhde
////////////////////////////////////////////////////////////////////////////////

#include "ExecutionBlockImpl.h"

#include "Basics/Common.h"

#include "Aql/AqlItemBlock.h"
#include "Aql/InputAqlItemRow.h"
#include "Aql/ExecutionState.h"
#include "Aql/ExecutorInfos.h"
#include "Aql/FilterExecutor.h"
#include "Aql/ExecutionEngine.h"

using namespace arangodb;
using namespace arangodb::aql;

template <class Executor>
ExecutionBlockImpl<Executor>::ExecutionBlockImpl(ExecutionEngine* engine,
                                                 ExecutionNode const* node,
                                                 ExecutorInfos&& infos)
    : ExecutionBlock(engine, node),
      _infos(infos),
      _blockFetcher(this),
      _rowFetcher(_blockFetcher),
      _executor(_rowFetcher, _infos)
    {}

template<class Executor>
ExecutionBlockImpl<Executor>::~ExecutionBlockImpl() {
  if(_outputItemRow){
    std::unique_ptr<AqlItemBlock> block = _outputItemRow->stealBlock();
    if(block) {
      AqlItemBlock* block_pointer = block.release();
      _engine->_itemBlockManager.returnBlock(block_pointer);
    }
  }
}

template<class Executor>
std::pair<ExecutionState, std::unique_ptr<AqlItemBlock>> ExecutionBlockImpl<Executor>::getSome(size_t atMost) {

  if(!_outputItemRow) {
    auto newBlock = this->requestBlock(atMost, _infos.numberOfOutputRegisters());
    _outputItemRow = std::make_unique<OutputAqlItemRow>(
        std::unique_ptr<AqlItemBlock>{newBlock}, _infos);
  }

  // TODO It's not very obvious that `state` will be initialized, because
  // it's not obvious that the loop will run at least once (e.g. after a
  // WAITING). It should, but I'd like that to be clearer. Initializing here
  // won't help much because it's unclear whether the value will be correct.
  ExecutionState state;
  std::unique_ptr<OutputAqlItemRow> row;  // holds temporary rows

  TRI_ASSERT(atMost > 0);

  while (!_outputItemRow->isFull()) {
    state = _executor.produceRow(*_outputItemRow);
    // TODO I'm not quite happy with produced(). Internally in OutputAqlItemRow,
    // this means "we copied registers from a source row", while here, it means
    // "the executor wrote its values".
    if (_outputItemRow && _outputItemRow->produced()) {
      _outputItemRow->advanceRow();
    }

    if (state == ExecutionState::WAITING) {
      return {state, nullptr};
    }

    if (state == ExecutionState::DONE) {
      auto outputBlock = _outputItemRow->stealBlock();
      // TODO OutputAqlItemRow could get "reset" and "isValid" methods and be reused

      // This is not strictly necessary here, as we shouldn't be called again
      // after DONE.
      _outputItemRow.reset(nullptr);

      return {state, std::move(outputBlock)};
    }
  }

  TRI_ASSERT(state == ExecutionState::HASMORE);
  TRI_ASSERT(_outputItemRow->numRowsWritten() == atMost);

  auto outputBlock = _outputItemRow->stealBlock();
  // TODO OutputAqlItemRow could get "reset" and "isValid" methods and be reused
  _outputItemRow.reset(nullptr);

  return {state, std::move(outputBlock)};
}

template <class Executor>
std::pair<ExecutionState, size_t> ExecutionBlockImpl<Executor>::skipSome(
    size_t atMost) {
  // TODO IMPLEMENT ME, this is a stub!

  auto res = getSome(atMost);

  return {res.first, res.second->size()};
}

template class ::arangodb::aql::ExecutionBlockImpl<FilterExecutor>;