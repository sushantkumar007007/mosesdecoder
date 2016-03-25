/*
 * PhraseLR.h
 *
 *  Created on: 22 Mar 2016
 *      Author: hieu
 */

#pragma once
#include "LRState.h"
#include "../../InputPathBase.h"

namespace Moses2 {

class PhraseBasedReorderingState : public LRState
{
public:
  const InputPathBase *prevPath;

  PhraseBasedReorderingState(const LRModel &config,
		  LRModel::Direction dir,
		  size_t offset);

  size_t hash() const;
  virtual bool operator==(const FFState& other) const;

  virtual std::string ToString() const
  {
	  return "";
  }

  void Expand(const System &system,
		  const LexicalReordering &ff,
		  const Hypothesis &hypo,
		  size_t phraseTableInd,
		  Scores &scores,
		  FFState &state) const;

protected:
  size_t  GetOrientation(Range const& cur) const;
  size_t  GetOrientation(Range const& prev, Range const& cur) const;

};

} /* namespace Moses2 */
