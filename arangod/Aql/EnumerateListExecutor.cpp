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

#include "EnumerateListExecutor.h"
#include <lib/Logger/LogMacros.h>

#include "Basics/Common.h"

#include "Aql/AqlValue.h"
#include "Aql/ExecutorInfos.h"
#include "Aql/InputAqlItemRow.h"
#include "Aql/SingleRowFetcher.h"
#include "Basics/Exceptions.h"

using namespace arangodb;
using namespace arangodb::aql;

EnumerateListExecutorInfos::EnumerateListExecutorInfos(
    RegisterId inputRegister, RegisterId outputRegister,
    RegisterId nrOutputRegisters, RegisterId nrInputRegisters,
    std::unordered_set<RegisterId> const registersToClear,
    transaction::Methods* trx)
    : ExecutorInfos(inputRegister, outputRegister, nrInputRegisters,
                    nrOutputRegisters, registersToClear),
      _trx(trx) {
  TRI_ASSERT(trx != nullptr);
}

EnumerateListExecutorInfos::~EnumerateListExecutorInfos() {}

transaction::Methods* EnumerateListExecutorInfos::trx() const { return _trx; }

EnumerateListExecutor::EnumerateListExecutor(Fetcher& fetcher,
                                             EnumerateListExecutorInfos& infos)
    : _fetcher(fetcher), _infos(infos){};
EnumerateListExecutor::~EnumerateListExecutor() = default;

ExecutionState EnumerateListExecutor::produceRow(OutputAqlItemRow& output) {
  while (true) {
    // HIT in first run, because pos and length are initiliazed
    // both with 0
    LOG_DEVEL << "========== START ===========";
    LOG_DEVEL << "pos is : " << _inputArrayPosition;
    LOG_DEVEL << "length is : " << _inputArrayLength;
    LOG_DEVEL << "===========================";

    if (_inputArrayPosition == _inputArrayLength ||
        _inputArrayPosition == _inputArrayLength - 1) {
      LOG_DEVEL << "got soemthing new, beginning";
      // we need to set position back to zero
      // because we finished iterating over existing array
      // element and need to refetch another row
      _inputArrayPosition = 0;
      std::tie(_rowState, _currentRow) = _fetcher.fetchRow();
      LOG_DEVEL << "X - ROW STATE IS: " << _rowState;
      LOG_DEVEL << "ARE WE INITIALIZED? :" << _currentRow.isInitialized();
      if (_rowState == ExecutionState::WAITING) {
        LOG_DEVEL << "we're just waiting.";
        return _rowState;
      }
    }

    if (!_currentRow.isInitialized()) {
      TRI_ASSERT(_rowState == ExecutionState::DONE);
      return _rowState;
    }

    AqlValue const& value = _currentRow.getValue(_infos.getInput());

    if (_inputArrayPosition == 0) {
      // store the length into a local variable
      // so we don't need to calculate length every time
      if (value.isDocvec()) {
        _inputArrayLength = value.docvecSize();
      } else {
        _inputArrayLength = value.length();
      }
      LOG_DEVEL << "Position is zero, array length is: " << _inputArrayLength;
    } else {
      // read current array length
      LOG_DEVEL << "Position is: " << _inputArrayLength
                << ", array length is: " << _inputArrayLength;
    }

    if (_inputArrayLength == 0) {
      LOG_DEVEL << "length is zero, skipping";
      continue;
    } else if (_inputArrayLength == _inputArrayPosition - 1) {
      // we reached the end, forget all state
      LOG_DEVEL << "end reached. re-initializing.";
      initialize();

      if (_rowState == ExecutionState::HASMORE) {
        continue;
      } else {
        return _rowState;
      }
    } else {
      //  for (size_t j = _inputArrayPosition; j < _inputArrayLength; j++) {
      LOG_DEVEL << "currently at position: " << _inputArrayPosition;
      bool mustDestroy = false;

      AqlValue innerValue =
          getAqlValue(value, _inputArrayPosition, mustDestroy);
      AqlValueGuard guard(innerValue, mustDestroy);

      output.setValue(_infos.getOutput(), _currentRow, innerValue);
      // TODO: clarify if we need to release the guard

      // set position to +1 for next iteration after new fetchRow
      _inputArrayPosition++;

      if (_inputArrayPosition < _inputArrayLength || _rowState == ExecutionState::HASMORE) {
        return ExecutionState::HASMORE;
      }
      return ExecutionState::DONE;
    }
  }
}

void EnumerateListExecutor::initialize() {
  _inputArrayLength = 0;
  _inputArrayPosition = 0;
  _currentRow = InputAqlItemRow{CreateInvalidInputRowHint{}};
}

/// @brief create an AqlValue from the inVariable using the current _index
AqlValue EnumerateListExecutor::getAqlValue(AqlValue const& inVarReg,
                                            size_t const& pos,
                                            bool& mustDestroy) {
  TRI_IF_FAILURE("EnumerateListBlock::getAqlValue") {
    THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
  }

  return inVarReg.at(_infos.trx(), pos, mustDestroy, true);
}