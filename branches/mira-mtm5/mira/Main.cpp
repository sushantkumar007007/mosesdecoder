/***********************************************************************
Moses - factored phrase-based language decoder
Copyright (C) 2010 University of Edinburgh

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
***********************************************************************/

#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

#include <boost/program_options.hpp>

#include "FeatureVector.h"
#include "StaticData.h"
#include "ChartTrellisPathList.h"
#include "ChartTrellisPath.h"
#include "ScoreComponentCollection.h"
#include "Decoder.h"
#include "Optimiser.h"

using namespace Mira;
using namespace std;
using namespace Moses;
namespace po = boost::program_options;

void OutputNBestList(const MosesChart::TrellisPathList &nBestList, const TranslationSystem* system, long translationId);

bool loadSentences(const string& filename, vector<string>& sentences) {
  ifstream in(filename.c_str());
  if (!in) return false;
  string line;
  while(getline(in,line)) {
    sentences.push_back(line);
  }
  return true;
}

int main(int argc, char** argv) {
  bool help;
  int verbosity;
  string mosesConfigFile;
  string inputFile;
  vector<string> referenceFiles;
  po::options_description desc("Allowed options");
  desc.add_options()
        ("help",po::value( &help )->zero_tokens()->default_value(false), "Print this help message and exit")
        ("config,f",po::value<string>(&mosesConfigFile),"Moses ini file")
        ("verbosity,v", po::value<int>(&verbosity)->default_value(0), "Verbosity level")
        ("input-file,i",po::value<string>(&inputFile),"Input file containing tokenised source")
        ("reference-files,r", po::value<vector<string> >(&referenceFiles), "Reference translation files for training");

  po::options_description cmdline_options;
  cmdline_options.add(desc);
  po::variables_map vm;
  po::store(po::command_line_parser(argc,argv).
            options(cmdline_options).run(), vm);
  po::notify(vm);

  

  if (help) {
    std::cout << "Usage: " + string(argv[0]) +  " -f mosesini-file -i input-file -r reference-file(s) [options]" << std::endl;
    std::cout << desc << std::endl;
    return 0;
  }
  
  if (mosesConfigFile.empty()) {
    cerr << "Error: No moses ini file specified" << endl;
    return 1;
  }

  if (inputFile.empty()) {
    cerr << "Error: No input file specified" << endl;
    return 1;
  }

  if (!referenceFiles.size()) {
    cerr << "Error: No reference files specified" << endl;
    return 1;
  }



  //load input and references 
  vector<string> inputSentences;
  if (!loadSentences(inputFile, inputSentences)) {
    cerr << "Error: Failed to load input sentences from " << inputFile << endl;
    return 1;
  }

  vector< vector<string> > referenceSentences(referenceFiles.size());
  for (size_t i = 0; i < referenceFiles.size(); ++i) {
    if (!loadSentences(referenceFiles[i], referenceSentences[i])) {
      cerr << "Error: Failed to load reference sentences from " << referenceFiles[i] << endl;
      return 1;
    }
    if (referenceSentences[i].size() != inputSentences.size()) {
      cerr << "Error: Input file length (" << inputSentences.size() <<
        ") != (" << referenceSentences[i].size() << ") length of reference file " << i  <<
          endl;
      return 1;
    }
  }
  //initialise moses
  initMoses(mosesConfigFile, verbosity);//, argc, argv);

  //Main loop:
  ScoreComponentCollection cumulativeWeights;
  MosesDecoder* decoder = new MosesDecoder() ;
  Optimiser* optimiser = new Perceptron();
  size_t epochs = 1;
  size_t modelHypoCount = 10;
  size_t hopeHypoCount = 10;
  size_t fearHypoCount = 10;
  size_t iterations;
  
	
  for (size_t epoch = 0; epoch < epochs; ++epoch) {
    //TODO: batch
    for (size_t sid = 0; sid < inputSentences.size(); ++sid) {
      ++iterations;
      const string& input = inputSentences[sid];
      const vector<string>& refs = referenceSentences[sid];

      vector<vector<ScoreComponentCollection > > allScores(1);
      vector<vector<float> > allLosses(1);

			vector<const Moses::ScoreComponentCollection*> scores;
      vector<float> totalScores;

			StaticData &staticNonConst = StaticData::InstanceNonConst();

			// MODEL
			PARAM_VEC bleuWeight(1, "0");
			staticNonConst.GetParameter()->OverwriteParam("-weight-b", bleuWeight);
			staticNonConst.ReLoadParameter();
      scores.clear(); totalScores.clear();
      decoder->getNBest(input, modelHypoCount, scores, totalScores);
      for (size_t i = 0; i < scores.size(); ++i) {
        allScores[0].push_back(*scores[i]);
        allLosses[0].push_back(totalScores[i] + decoder->getBleuScore(*scores[i]));
      }

			// HOPE
			bleuWeight[0] = "+1";
			staticNonConst.GetParameter()->OverwriteParam("-weight-b", bleuWeight);
			staticNonConst.ReLoadParameter();
      scores.clear(); totalScores.clear();
			decoder->getNBest(input, hopeHypoCount, scores, totalScores);
      for (size_t i = 0; i < scores.size(); ++i) {
        allScores[0].push_back(*scores[i]);
        allLosses[0].push_back(totalScores[i]);
      }
      assert(scores.size());
      const ScoreComponentCollection* oracleScores = scores[0];
      float oracleLoss = totalScores[0];
			
			// FEAR
			bleuWeight[0] = "-1";
			staticNonConst.GetParameter()->OverwriteParam("-weight-b", bleuWeight);
			staticNonConst.ReLoadParameter();
      scores.clear(); totalScores.clear();
			decoder->getNBest(input, fearHypoCount, scores, totalScores);
      for (size_t i = 0; i < scores.size(); ++i) {
        allScores[0].push_back(*scores[i]);
        allLosses[0].push_back(totalScores[i] + 2*decoder->getBleuScore(*scores[i])); 
      }

      //set bleu score to zero in allScores
      //set loss for each sentence ss oracleloss - rawsentenceloss
      for (size_t i = 0; i < allScores.size(); ++i) {
        for (size_t j = 0; j < allScores[i].size(); ++j) {
          decoder->setBleuScore(allScores[i][j],0);
          allLosses[i][j] = oracleLoss - allLosses[i][j];
        }
      }

						
			
      //run optimiser
		  ScoreComponentCollection mosesWeights = decoder->getWeights();	
			optimiser->updateWeights(mosesWeights
															, allScores
															, allLosses
															, *oracleScores);
			
      //update moses weights
      decoder->setWeights(mosesWeights);

      cumulativeWeights.PlusEquals(mosesWeights);
      cerr << "Cumulative weights: " << cumulativeWeights << endl;
      cerr << "Averaging: TODO " << endl;

			
			decoder->cleanup();
    }
  }
  


  exit(0);
}

