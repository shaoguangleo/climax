#include <iostream>
#include <fstream>
#include <math.h>

#include "gcp/program/Program.h"

#include "gcp/models/GaussianClusterModel.h"
#include "gcp/models/Generic2DGaussian.h"

#include "gcp/util/Exception.h"
#include "gcp/util/LogStream.h"
#include "gcp/util/IoLock.h"
#include "gcp/util/GaussianVariate.h"
#include "gcp/util/Range.h"
#include "gcp/util/Timer.h"

#include "gcp/pgutil/PgUtil.h"

#include "gcp/fftutil/DataSet.h"
#include "gcp/fftutil/Dft2d.h"
#include "gcp/fftutil/Image.h"
#include "gcp/fftutil/Model.h"

#include "gcp/util/String.h"

#include "gcp/datasets/DataSet1D.h"

#include "gcp/datasets/DataSetManager.h"
#include "gcp/fftutil/ObsManager.h"
#include "gcp/models/ModelManager.h"

#include "cpgplot.h"

using namespace std;
using namespace gcp::datasets;
using namespace gcp::util;
using namespace gcp::program;
using namespace gcp::models;

KeyTabEntry Program::keywords[] = {
  { "file",           "",  "s", "FITS file to read in"},
  { "nburn",      "1000",  "i", "Number of burn-in samples"},
  { "ntry",      "10000",  "i", "Number of trials"},
  { "nmodelthread",  "0",  "i", "Number of threads in the model pool"},
  { "ndatathread",   "0",  "i", "Number of threads in the data pool"},
  { "cpus",        "any",  "s", "List of CPUs to partition threads among"},

  { END_OF_KEYWORDS,END_OF_KEYWORDS,END_OF_KEYWORDS,END_OF_KEYWORDS},
};

void Program::initializeUsage() {};

void parseFile(std::string modelFile,   ThreadPool* modelPool, ThreadPool* dataPool);
void processFile(std::string modelFile, ThreadPool* modelPool, ThreadPool* dataPool);
void processLine(String& line, ThreadPool* modelPool, ThreadPool* dataPool);

void getObsName(String& line, std::string& name);
void getDataSetNameAndType(String& line, std::string& name, std::string& type);
void getModelNameAndType(String& line, std::string& name, std::string& type);
void getTokVal(String& line, String& tok, String& val);
void parseCorrelationVariables(String& token, std::string& varName1, std::string& varName2);
void parseVarname(String& mVarName, String& modelName, String& varName);
void parseUniformPrior(String& val, String& min, String& max, String& units);
void parseGaussianPrior(String& val, String& mean, String& sigma, String& units);
void parseVal(String& val, String& value, String& units);

void parseVariableAssignment(String& tok, String& val);
void parseObsVariableAssignment(ObsInfo* obs, String& varName, String& valStr);
void parseDataSetVariableAssignment(DataSet* dataSet, String& varName, String& valStr);
void parseModelVariableAssignment(String& tok, String& valStr);

void parseVariableIncrement(String& tok, String& val);
void parseDataSetVariableIncrement(DataSet* dataSet, String& varName, String& valStr);

void checkIfNameAlreadyExists(std::string name);
void runMarkov(unsigned nBurn, unsigned nTry);
void updateHessian(Probability startProb);
bool updateHessian2(Probability startProb);
bool updateHessian3(Probability startProb, double frac=0.1, double gamme=1.0);
bool getNextAcceptedSample(unsigned iVar, Vector<double>& tmp, Vector<double>& mean, Matrix<double>& invCov, Probability& prob);
void getNextSample(Vector<double>& sample, Probability& prob);
void getOutputArgs(String& line);
void getGenDataArgs(String& line);
void getComputeChisqArgs(String& line);
void displayDataSet1D(DataSet* ds, double xvp1, double xvp2, double yvp1, double yvp2);
void displayDataSet2D(DataSet* ds, double xvp1, double xvp2, double yvp1, double yvp2);
void computeChisq();
bool isNumeric(char c);
bool assignmentDependsOnDataset(String& inputVal);
String getVariableValueString(String& inputValStr);
void loadData();
void processDatasetDependentLines();

//-----------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------


ModelManager   mm_;
DataSetManager dm_;
ObsManager     om_;
bool compChisq_ = false;
bool datasetsInitialized_ = false;
unsigned nBurn_ = 0;
unsigned nTry_  = 0;

// A vector of lines to process after data have been read in

std::vector<std::string> datasetDependentLines_;

/**.......................................................................
 * Main -- parse a config file and execute its instructions
 */
int Program::main()
{
  std::string file  = Program::getStringParameter("file");
  unsigned nTry     = Program::getIntegerParameter("ntry");
  unsigned nBurn    = Program::getIntegerParameter("nburn");
  unsigned nModelThread = Program::getIntegerParameter("nmodelthread");
  unsigned nDataThread = Program::getIntegerParameter("ndatathread");
  
  std::vector<unsigned> cpus;
  if(Program::getStringParameter("cpus") != "any") {
    String rangeStr(Program::getStringParameter("cpus"));
    Range<unsigned> range;
    cpus = range.extractIndexRange(rangeStr, 0, 24, 0, true);

    for(unsigned i=0; i < cpus.size(); i++) {
      COUT("cpu " << i << " = " << cpus[i]);
    }
  }

  unsigned wasError = 0;

  ThreadPool* modelPool = 0;
  ThreadPool* dataPool  = 0;

  if(nModelThread > 0) {
    modelPool = new ThreadPool(nModelThread);
    modelPool->spawn();
  }

  if(nDataThread > 0) {
    dataPool = new ThreadPool(nDataThread);
    dataPool->spawn();
  }

  try {

    parseFile(file, modelPool, dataPool);

    if(mm_.genData()) {
      mm_.generateFakeData();
    } else if(compChisq_) {
      computeChisq();
    } else {

      mm_.setThreadPool(modelPool);

      if(nBurn_ > 0) {
	COUTCOLOR("Overriding command line nburn=" << nBurn << " with " << nBurn_, "green");
	nBurn = nBurn_;
      }
      
      if(nTry_ > 0) {
	COUTCOLOR("Overriding command line ntry=" << nTry << " with " << nTry_, "green");
	nTry = nTry_;
      }

      mm_.initializeForMarkovChain(nTry + nBurn, nTry, file);
      runMarkov(nBurn, nTry);
    }
  } catch(Exception& err) {
    COUT(err.what());
    wasError = 1;
  } catch(...) {
    COUT("Caught an unknown exception");
    wasError = 1;
  }

  if(modelPool) 
    delete modelPool;

  if(dataPool) 
    delete dataPool;

  return wasError;
}

void processFile(std::string modelFile, ThreadPool* modelPool, ThreadPool* dataPool) 
{
  static String line;
  
  std::ifstream fin;
  fin.open(modelFile.c_str(), ios::in);
  
  if(!fin) {
    ThrowError("Unable to open file: " << modelFile);
  }
  
  //------------------------------------------------------------
  // Iterate through the file
  //------------------------------------------------------------
  
  while(!fin.eof()) {
    line.initialize();
    getline(fin, line.str());
    
    line = line.findNextInstanceOf("", false, ";", true, true);
    line.advanceToNextNonWhitespaceChar();
    
    // Don't process comment lines
    
    if(line[0] == '/')
      continue;
    
    processLine(line, modelPool, dataPool);
  }
  
  fin.close();
}

/**.......................................................................
 * Parse a config file
 */
void parseFile(std::string modelFile,   ThreadPool* modelPool, ThreadPool* dataPool)
{
  processFile(modelFile, modelPool, dataPool);

  // Now load any data sets that were specified
  
  loadData();

  // Check for position information

  dm_.checkPosition();

  // Only once data sets have been loaded, can we make variable
  // assignments that depend on them

  processDatasetDependentLines();
  
  // Finally, check model setups for sense

  mm_.checkSetup();
}

/**.......................................................................
 * Called to process a single line from a config file
 */
void processLine(String& line, ThreadPool* modelPool, ThreadPool* dataPool)
{
  //------------------------------------------------------------
  // Add model line
  //------------------------------------------------------------

  if(line.contains("addmodel")) {

    std::string name, type;

    getModelNameAndType(line, name, type);
    checkIfNameAlreadyExists(name);

    Model* model = mm_.addModel(type, name);
    model->setThreadPool(modelPool);

    //------------------------------------------------------------
    // Add dataset line
    //------------------------------------------------------------

  } else if(line.contains("adddataset")) {

    std::string name, type;

    getDataSetNameAndType(line, name, type);
    checkIfNameAlreadyExists(name);

    DataSet* dataSet = dm_.addDataSet(type, name);
    dataSet->setThreadPool(dataPool);

    //------------------------------------------------------------
    // Add obs line
    //------------------------------------------------------------

  } else if(line.contains("addobs")) {

    std::string name, type;

    getObsName(line, name);
    checkIfNameAlreadyExists(name);

    ObsInfo* obs = om_.addObs(name);

    //------------------------------------------------------------
    // Output file line
    //------------------------------------------------------------
      
  } else if(line.contains("output ")) {

    getOutputArgs(line);
	
    //------------------------------------------------------------
    // Generate fake data line
    //------------------------------------------------------------
      
  } else if(line.contains("gendata")) {
      
    getGenDataArgs(line);
      
    //------------------------------------------------------------
    // Compute chisq line
    //------------------------------------------------------------
      
  } else if(line.contains("comp")) {
      
    getComputeChisqArgs(line);
      
    //------------------------------------------------------------
    // Include line
    //------------------------------------------------------------

  } else if(line.contains("include")) {
      
    // Skip the include token

    line.findNextString();

    String fileName = line.findNextString();
    fileName.strip(' ');
    fileName.expandTilde();

    processFile(fileName.str(), modelPool, dataPool);
      
    //------------------------------------------------------------
    // Other lines we recognize
    //------------------------------------------------------------

  } else if(!line.isEmpty()) {
      
    //------------------------------------------------------------
    // Token = value statement
    //------------------------------------------------------------
      
    if(line.contains("=")) {

      String tok, val;
      getTokVal(line, tok, val);

      // Intercept global variable assignments

      if(!tok.contains(".")) {
	if(tok.contains("nburn")) {
	  nBurn_ = val.toInt();
	  return;
	} else if(tok.contains("ntry")) {
	  nTry_ = val.toInt();
	  return;
	}
      }

      // Don't proceed if this assignment depends on a dataset
      // parameter that has not yet been read in

      if(assignmentDependsOnDataset(val) && !datasetsInitialized_) {
	datasetDependentLines_.push_back(line.str());
	return;
      }

      //------------------------------------------------------------
      // Check for different types of tokens.
      //
      // Correlations are specified like:          var1 * var2 = val
      // Variable assignmemnts are specified like: var = val
      // 
      //------------------------------------------------------------

      if(tok.contains("*")) {

	std::string varName1, varName2;
	parseCorrelationVariables(tok, varName1, varName2);
	mm_.addComponentCorrelation(varName1, varName2, val.toDouble());

	// Else this is either an increment or an assignment

      } else if(line.contains("+=")) {
	parseVariableIncrement(tok, val);
      } else {
	parseVariableAssignment(tok, val);
      }
	
    } else {
      ThrowError("Unrecognized directive: " << line);
    }
      
  }
}

void getModelNameAndType(String& line, std::string& name, std::string& type)
{
  // Get to the first non-whitespace char

  line.advanceToNextNonWhitespaceChar();

  // Skip the addmodel token

  line.findNextString();

  String nt;
  do {

    nt = line.findNextString();

    if(!nt.isEmpty()) {
      String tok,val;
      getTokVal(nt, tok, val);

      if(tok.str() == "name") {
	name = val.str();
      } else if(tok.str() == "type") {
	type = val.str();
      } else {
	ThrowError("Unrecognized token: " << tok);
      }
    }
  } while(!nt.isEmpty());
}

void getDataSetNameAndType(String& line, std::string& name, std::string& type)
{
  // Get to the first non-whitespace char

  line.advanceToNextNonWhitespaceChar();

  // Skip the addmodel token

  line.findNextString();

  String nt;
  do {

    nt = line.findNextString();

    if(!nt.isEmpty()) {
      String tok,val;
      getTokVal(nt, tok, val);

      if(tok.str() == "name") {
	name = val.str();
      } else if(tok.str() == "type") {
	type = val.str();
      } else {
	ThrowError("Unrecognized token: " << tok);
      }
    }
  } while(!nt.isEmpty());
}

/**.......................................................................
 * Parse an expression of the form addobs name=whatever
 */
void getObsName(String& line, std::string& name)
{
  // Get to the first non-whitespace char

  line.advanceToNextNonWhitespaceChar();

  // Skip the addobs token

  line.findNextString();

  String nt;
  do {

    nt = line.findNextString();

    if(!nt.isEmpty()) {
      String tok,val;
      getTokVal(nt, tok, val);

      if(tok.str() == "name") {
	name = val.str();
      } else {
	ThrowError("Unrecognized token: " << tok);
      }
    }
  } while(!nt.isEmpty());
}

void getOutputArgs(String& line)
{
  line.advanceToNextNonWhitespaceChar();

  // Skip the output token
  
  line.findNextString();
  
  String nt;
  do {

    nt = line.findNextString();

    if(!nt.isEmpty()) {
      String tok,val;
      getTokVal(nt, tok, val);


      if(tok.str() == "file") {
	mm_.openOutputFile(val.str(), "");
      } else {
	ThrowError("Unrecognized token: " << tok);
      }
    }

  } while(!nt.isEmpty());

}

void getGenDataArgs(String& line)
{
  line.advanceToNextNonWhitespaceChar();

  // Skip the output token
  
  line.findNextString();
  
  String nt;
  do {

    nt = line.findNextString();

    if(!nt.isEmpty()) {
      String tok,val;
      getTokVal(nt, tok, val);
      
      tok.strip(' ');
      val.strip(' ');

      if(tok.str() == "file") {
	mm_.setGenDataFile(val.str());
	mm_.setGenData(true);

      } else if(tok.str() == "dataset") {
	DataSet* dataSet = dm_.getDataSet(val.str());
	mm_.setGenDataDataSet(dataSet);
      } else if(tok.str() == "sigma") {
	mm_.setGenDataSigma(val.toDouble());
      } else {
	ThrowError("Unrecognized token: '" << tok << "'");
      }
    }

  } while(!nt.isEmpty());

}

void getComputeChisqArgs(String& line)
{

  String tok,val;
  getTokVal(line, tok, val);
      
  tok.strip(' ');
  val.strip(' ');
  
  if(tok.str() == "comp") {
    
    if(val.str() == "true") {
      compChisq_ = true;
    } else {
      compChisq_ = false;
    }
    
  } else {
    ThrowError("Unrecognized token: '" << tok << "'");
  }

}

/**.......................................................................
 * Get the value of the next token
 */
void getTokVal(String& line, String& tok, String& val)
{
  line.advanceToNextNonWhitespaceChar();

  if(line.contains("+="))
    tok = line.findNextInstanceOf("", false, "+=", true, true);
  else
    tok = line.findNextInstanceOf("", false, "=", true, true);

  if(tok.isEmpty())
    ThrowError("Empty token name");

  val = line.remainder();

  if(val.isEmpty())
    ThrowError("Empty value");

  // Expand any shell ~ directives before returning

  val.expandTilde();
}

void parseCorrelationVariables(String& token, 
			       std::string& varName1, std::string& varName2)
{
  token.strip(' ');

  String varStr1, varStr2;
  varStr1 = token.findNextInstanceOf("", false, "*", true, false);
  varStr2 = token.findNextInstanceOf("*", true, " ", false, true);

  varName1 = varStr1.str();
  varName2 = varStr2.str();
}

void parseVarname(String& mVarName, String& modelName, String& varName)
{
  if(!mVarName.contains(".")) {
    ThrowError("Invalid variable name: " << mVarName << " (should be model.varname)");
  }

  modelName = mVarName.findNextInstanceOf(" ", false, ".", true, false);
  varName   = mVarName.findNextInstanceOf(".", true, " ", false, false);

  modelName.strip(' ');
  varName.strip(' ');
}

/**.......................................................................
 * Parse a variable assignment of the form 'var = expr;'
 */
void parseVariableAssignment(String& tok, String& valStr)
{
  // First parse the variable name

  tok.strip(' ');

  String ownerName, varName;
  parseVarname(tok, ownerName, varName);

  // Now check if the owner is a model or a dataset, or neither

  Model*   model   = 0;
  DataSet* dataSet = 0;
  ObsInfo* obs     = 0;

  try {
    model = mm_.getModel(ownerName.str());
  } catch(Exception& err) {
  }

  if(model)
    return parseModelVariableAssignment(tok, valStr);

  // If we got here, then no model matches -- check datasets

  try {
    dataSet = dm_.getDataSet(ownerName.str());
  } catch(Exception& err) {
  }

  if(dataSet)
    return parseDataSetVariableAssignment(dataSet, varName, valStr);

  try {
    obs = om_.getObs(ownerName.str());
  } catch(Exception& err) {
  }

  if(obs)
    return parseObsVariableAssignment(obs, varName, valStr);

  ThrowColorError("No model or dataset matches the name: '" << ownerName << "'", "red");
}

/**.......................................................................
 * Parse an assignment to obs parameters
 */
void parseObsVariableAssignment(ObsInfo* obs, String& varName, String& valStr)
{
  // Next parse the expression

  valStr.strip(' ');

  // If this is a numeric string, then also check for units

  if(isNumeric(valStr[0])) {

    String numStr  = valStr.findNextNumericString(":,.");
    String unitStr = valStr.remainder();

    obs->setParameter(varName.str(), numStr.str(), unitStr.str());

  } else {
    obs->setParameter(varName.str(), valStr.str());
  }

}

/**.......................................................................
 * Parse an assignment to a dataset parameters
 */
void parseDataSetVariableAssignment(DataSet* dataSet, String& varName, String& valStr)
{
  // Next parse the expression

  valStr.strip(' ');

  //------------------------------------------------------------
  // Check for special variables first.  These are assignment-only,
  // like dataset parameters, but we have to check for them
  // separately, since we want to allow things like ra = 12:34:34.2
  // which would otherwise get interpreted as a prior specification
  //------------------------------------------------------------

  if(varName.contains("ra") && valStr.contains(":")) {
    dataSet->setParameter(varName.str(), valStr.str());
    return;
  } else if(varName.contains("dec") && valStr.contains(":")) {
    dataSet->setParameter(varName.str(), valStr.str());
    return;
  }

  //------------------------------------------------------------
  // Check for obs keywords
  //------------------------------------------------------------

  if(varName.contains("obs")) {
    COUT("Inside tModelParser getObs: " << om_.getObs(valStr.str())->antennas_[0].getReferenceLla());

    dataSet->setObs(om_.getObs(valStr.str()));
    return;
  }

  // If this is a numeric string, then also check for units

  if(isNumeric(valStr[0])) {

    String numStr  = valStr.findNextNumericString();
    String unitStr = valStr.remainder();

    dataSet->setParameter(varName.str(), numStr.str(), unitStr.str());

  } else {
    dataSet->setParameter(varName.str(), valStr.str());
  }

}

/**.......................................................................
 * Parse an assignment to a model variable
 */
void parseModelVariableAssignment(String& tok, String& inputValStr)
{
  Variate* var  = mm_.getVar(tok.str());

  String valStr = getVariableValueString(inputValStr);

  //------------------------------------------------------------
  // Check for special variables first.  These are assignment-only,
  // like dataset parameters, but we have to check for them
  // separately, since we want to allow things like ra = 12:34:34.2
  // which would otherwise get interpreted as a prior specification
  //------------------------------------------------------------

  if(tok.contains(".ra") && valStr.contains(":")) {
    var->setVal(valStr.str());
    return;
  } else if(tok.contains(".dec") && valStr.contains(":")) {
    var->setVal(valStr.str());
    return;
  }

  //------------------------------------------------------------
  // If the expression is of the form 'var = min:max;', then we assume
  // a uniform prior is being specified
  //------------------------------------------------------------

  if(valStr.contains(":")) {
    String minStr, maxStr, units;
    double min, max;
    
    parseUniformPrior(valStr, minStr, maxStr, units);

    var->setUnits(units.str());

    min = var->getVal(minStr.toDouble(), units.str());
    max = var->getVal(maxStr.toDouble(), units.str());

    var->prior().setType(Distribution::DIST_UNIFORM);
    var->prior().setUniformXMin(min);
    var->prior().setUniformXMax(max);

    try {
      var->setIsVariable(true);
    } catch(...) {
      ThrowColorError("You must set a fixed value for '" << tok.str() << "'", "red");
    }

    var->samplingDistribution().setType(Distribution::DIST_GAUSS);
    var->samplingDistribution().setGaussMean((max + min)/2);
    var->samplingDistribution().setGaussSigma(0.1*(max - min));
    
    //------------------------------------------------------------
    // Else if of the form 'var = mean +- sigma;' we assume a gaussian
    // prior is being specified
    //------------------------------------------------------------

  } else if(valStr.contains("+-")) {

    String meanStr, sigmaStr, units;
    double mean, sigma;

    parseGaussianPrior(valStr, meanStr, sigmaStr, units);

    var->setUnits(units.str());

    mean  = var->getVal(meanStr.toDouble(),  units.str());
    sigma = var->getVal(sigmaStr.toDouble(), units.str());

    var->prior().setType(Distribution::DIST_GAUSS);
    var->prior().setGaussMean(mean);
    var->prior().setGaussSigma(sigma);

    try {
      var->setIsVariable(true);
    } catch(...) {
      ThrowColorError("You must set a fixed value for '" << tok.str() << "'", "red");
    }

    var->samplingDistribution().setType(Distribution::DIST_GAUSS);
    var->samplingDistribution().setGaussMean(mean);
    var->samplingDistribution().setGaussSigma(sigma);

    //------------------------------------------------------------
    // Else this is just a simple 'var = val;' statement
    //------------------------------------------------------------

  } else {
    String valueStr, units;

    String testStr = valStr;
    testStr.strip(' ');

    if(testStr.isNumeric(testStr[0])) {
      parseVal(valStr, valueStr, units);
      var->setUnits(units.str());
      var->setVal(valueStr.toDouble(), units.str());
    } else {
      valStr.advanceToNextNonWhitespaceChar();
      valStr.strip(' ');
      var->setVal(valStr.str());
    }
  }
}

/**.......................................................................
 * Parse a variable assignment of the form 'var += expr;'
 */
void parseVariableIncrement(String& tok, String& valStr)
{
  // First parse the variable name

  tok.strip(' ');

  String ownerName, varName;
  parseVarname(tok, ownerName, varName);

  // Now check if the owner is a model or a dataset, or neither

  Model*   model = 0;
  DataSet* dataSet = 0;

  try {
    model = mm_.getModel(ownerName.str());
  } catch(Exception& err) {
  }

  if(model) {
    ThrowColorError("Component increment is not currently supported for models", "red");
  }

  // If we got here, then no model matches -- check datasets

  try {
    dataSet = dm_.getDataSet(ownerName.str());
  } catch(Exception& err) {
  }

  if(dataSet)
    return parseDataSetVariableIncrement(dataSet, varName, valStr);

  ThrowColorError("No dataset matches the name: '" << ownerName << "'", "red");
}

/**.......................................................................
 * Parse an increment to a dataset parameter
 */
void parseDataSetVariableIncrement(DataSet* dataSet, String& varName, String& valStr)
{
  // Next parse the expression

  valStr.strip(' ');

  //------------------------------------------------------------
  // Check for special variables first.  These are assignment-only,
  // like dataset parameters, but we have to check for them
  // separately, since we want to allow things like ra = 12:34:34.2
  // which would otherwise get interpreted as a prior specification
  //------------------------------------------------------------

  if(varName.contains("ra") && valStr.contains(":")) {
    dataSet->incrementParameter(varName.str(), valStr.str());
    return;
  } else if(varName.contains("dec") && valStr.contains(":")) {
    dataSet->incrementParameter(varName.str(), valStr.str());
    return;
  }

  // If this is a numeric string, then also check for units

  if(isNumeric(valStr[0])) {

    String numStr  = valStr.findNextNumericString();
    String unitStr = valStr.remainder();

    dataSet->incrementParameter(varName.str(), numStr.str(), unitStr.str());

  } else {
    dataSet->incrementParameter(varName.str(), valStr.str());
  }

}

/**.......................................................................
 * Parse a statement of the form 'var = min:max;' specifying a uniform
 * prior
 */
void parseUniformPrior(String& val, String& min, String& max, String& units)
{
  val.advanceToNextNonWhitespaceChar();

  min = val.findNextInstanceOf(" ", false, ":", true, true);
  val.advanceToNextNonWhitespaceChar();
  max   = val.findNextNumericString();
  units = val.remainder();

  units.strip(' ');
}

/**.......................................................................
 * Parse a statement of the form 'var = mean +- sigma;' specifying a
 * Gaussian prior
 */
void parseGaussianPrior(String& val, String& mean, String& sigma, String& units)
{
  val.advanceToNextNonWhitespaceChar();

  mean  = val.findNextInstanceOf(" ", false, "+-", true, true);
  val.advanceToNextNonWhitespaceChar();
  sigma = val.findNextNumericString();
  units = val.remainder();

  units.strip(' ');
}
 
/**.......................................................................
 * Parse a simple assignment of the form 'var = val;'
 */
void parseVal(String& val, String& value, String& units)
{
  val.advanceToNextNonWhitespaceChar();

  value = val.findNextNumericString();
  units = val.remainder();
  units.strip(' ');
}

void checkIfNameAlreadyExists(std::string name)
{
  // Check that no model or dataset already exists with that name

  bool exists=false;
  try {
    mm_.getModel(name);
    exists = true;
  } catch(...) {
  }

  if(exists) {
    ThrowColorError("A model with name: " << name << " has already been defined", "red");
  }

  try {
    dm_.getDataSet(name);
    exists = true;
  } catch(...) {
  }

  if(exists) {
    ThrowColorError("A dataset with name: " << name << " has already been defined", "red");
  }
}

/**.......................................................................
 * Run a Markov chain with the currently defined models and datasets
 */
void runMarkov(unsigned nBurn, unsigned nTry)
{
  bool accept;
  double alpha;
  Probability likeCurr, likePrev, propDensCurr, propDensPrev;
  GaussianVariate gVar;
  bool first = true;
  bool printNewline = true;
  unsigned iLastUpdate=0;
  unsigned nAcceptedSinceLastUpdate=0, nTrySinceLastUpdate=0;
  double acceptFrac = 0.0;
  double acceptFracLowLim, acceptFracHighLim;
  unsigned nPerUpdate = 100;
  unsigned nUpdate = 0;
  Timer timer;
  timer.start();

  Timer sampleTimer, likeTimer;
  double sampleTime=0.0, likeTime=0.0;

  Probability rat;
  ChisqVariate chisq;

  acceptFracLowLim  = mm_.nVar() > 1 ? 0.2 : 0.4;
  acceptFracHighLim = mm_.nVar() > 1 ? 0.4 : 0.6;

#if 1
  unsigned nreject=0;
#endif

  unsigned nPrint = nTry / 100;

  for(unsigned i=0; i < nTry; i++) {
    
    if(i % nPrint == 0) {

      if(i > nBurn && printNewline) {
	COUT("");
	printNewline = false;
      }

      COUTCOLORNNL(std::cout << "\r                                                                                                  " 
		   << "\rIteration: " << i << "/" << nTry << " (" << setprecision(2) << (100*(double)(i)/nTry) << "%)", "green");
      fflush(stdout);
    }

    alpha = Sampler::generateUniformSample(0.0, 1.0);

    dm_.clearModel();


    sampleTimer.start();
    mm_.sample();
    sampleTimer.stop();

    sampleTime += sampleTimer.deltaInSeconds();

    propDensCurr = mm_.priorPdf();


    rat = propDensCurr/(likePrev * propDensPrev);

#if 1
    if(!(rat > alpha)) {
      nreject++;
    }
#endif

    if(i==0 || rat > alpha) {

      likeTimer.start();
      likeCurr = dm_.likelihood(mm_);
      likeTimer.stop();

      likeTime += likeTimer.deltaInSeconds();

      rat *= likeCurr;

#if 0
      chisq = dm_.computeChisq();
#endif
      
      if(i > 0) {
	
	if(rat > alpha) {
	  accept = true;
	} else {
	  accept = false;
	}
	
      } else {
	accept = true;
      }
    } else {
      accept = false;
    }

    ++nTrySinceLastUpdate;

    if(accept) {

      likePrev     = likeCurr;
      propDensPrev = propDensCurr;

#if 1
      ++nAcceptedSinceLastUpdate;

      acceptFrac = (double)(nAcceptedSinceLastUpdate)/nTrySinceLastUpdate;

      if((i > 0) && (i < nBurn) && (i - iLastUpdate >= nPerUpdate)) {

	//	COUT("i = " << i << " iLastUpdate = " << iLastUpdate << " nBurn = " << nBurn);
	//	COUT("nAccept = " << nAcceptedSinceLastUpdate << " ntry = " << nTrySinceLastUpdate);

	// Accepted fraction is lower than we want -- use quadratic
	// estimator to tune the jumping distribution

	if(acceptFrac < acceptFracLowLim) {

	  COUT(std::endl << "Fraction accepted: " << (double)(nAcceptedSinceLastUpdate)/nTrySinceLastUpdate);
	  COUT("Updating hessian");

#if 1
	  // If we have already tuned the distribution several times,
	  // narrow the width to increase the number of samples
	  // accepted

	  if(nUpdate > 3) {
	    
	    Vector<double> currentSigmas = mm_.sigma_;
	    Vector<double> newSigmas = currentSigmas / 2;
	    mm_.setSamplingSigmas(newSigmas);
	    mm_.updateSamplingSigmas();

	  } else if(updateHessian2(likeCurr*propDensCurr)) {
	    ++nUpdate;
	  }
#else
	  Vector<double> currentSigmas = mm_.sigma_;
	  Vector<double> newSigmas = currentSigmas / 2;
	  mm_.setSamplingSigmas(newSigmas);
	  mm_.updateSamplingSigmas();
#endif

	  // Else accepted fraction is higher than we want.  Widen the
	  // jumping distribution so that more samples are rejected

	} else if(acceptFrac > acceptFracHighLim) {
	  COUT("Fraction accepted: " << acceptFrac);
	  COUT("Widening sampling distribution");
	  Vector<double> currentSigmas = mm_.sigma_;
	  Vector<double> newSigmas = currentSigmas * 2;
	  mm_.setSamplingSigmas(newSigmas);
	  mm_.updateSamplingSigmas();
	}

	nAcceptedSinceLastUpdate = 0;
	nTrySinceLastUpdate      = 0;
	iLastUpdate = i;
      }

#endif

      if(i > nBurn) {
	//	mm_.store(likeCurr);
      }

    } else {
      mm_.revert();
    }
  }

  timer.stop();

  COUTCOLOR("\nElapsed time = "            << timer.deltaInSeconds(), "yellow");
  COUTCOLOR("Time spent sampling: "        << sampleTime << " calculating likelihoods: " << likeTime, "yellow");
  COUTCOLOR("Time spent adding models: "   << dm_.addModelTime_, "yellow");
  COUTCOLOR("Time spent computing chisq: " << dm_.computeChisqTime_, "yellow");
  COUTCOLOR("Fraction accepted: "          <<(double)(mm_.nAccepted_)/(nTry - nBurn), "yellow");
  COUTCOLOR("Nrejected: "                  << nreject, "yellow");

  //  PgUtil::close();

  PgUtil::open("/xs");
  PgUtil::setOverplot(true);

  mm_.histogramVariates();

#if 1
  unsigned nVar = mm_.nVar();

  double xstart = 0.1;
  double ystart = 0.1;
  double xsep = 0.01;
  double ysep = 0.01;

  double dx = (1.0 - xstart - nVar * xsep)/nVar;
  double dy = (1.0 - ystart - nVar * ysep)/nVar;

  double xmin, xmax, ymin, ymax;

  if(nVar%2 == 0) {
    xmin = xstart + nVar/2 * (dx + xsep) + 3*xsep;
    xmax = xstart + nVar   * (dx + xsep) - xsep;
    ymin = ystart + nVar/2 * (dy + ysep) + ysep + 3*ysep;
  } else {
    xmin = xstart + (nVar/2 + 1) * (dx + xsep);
    ymin = ystart + (nVar/2 + 1) * (dy + ysep) + ysep;
  }
  
  xmax = xstart + nVar   * (dx + xsep) - xsep;
  ymax = 1.0;

  PgUtil::setVp(false);
  cpgsvp(xmin, xmax, ymin, ymax);

  // Just get the first data set and display

  DataSet* ds = dm_.dataSetMap_.begin()->second;

  if(ds->dataSetType_ & DataSetType::DATASET_1D) {
    cpgbox("BC", 0, 0, "BC", 0,0);
    displayDataSet1D(ds, xmin, xmax, ymin, ymax);
  } else {
    cpgwnad(0,1,0,1);
    float x1, x2, y1, y2;
    cpgqvp(0, &x1, &x2, &y1, &y2);
    displayDataSet2D(ds, x1, x2, y1, y2);
  }
#endif
}

/**.......................................................................
 * Display information for a 1D dataset
 */
void displayDataSet1D(DataSet* ds, double xvp1, double xvp2, double yvp1, double yvp2)
{
  mm_.setValues(mm_.bestFitSample_);
  dm_.clearModel();
  dm_.addModel(mm_);

  // We will display the residuals as 20% the height of the data plot

  double dyvp = (yvp2 - yvp1)/5;

  mm_.printModel("");

  COUTCOLOR(std::endl << dm_.computeChisq(), "green");

  PgUtil::setXTick(true);
  PgUtil::setYTick(true);
  PgUtil::setXTickLabeling(false);
  PgUtil::setYTickLabeling(true);

  cpgsvp(xvp1, xvp2, yvp1+dyvp, yvp2);
  ds->display();

  float xw1, xw2, yw1, yw2;
  cpgqwin(&xw1, &xw2, &yw1, &yw2);

  PgUtil::setTraceColor(2);
  PgUtil::setXTickLabeling(true);

  cpgsvp(xvp1, xvp2, yvp1, yvp1+dyvp);
  ds->displayResiduals();

  PgUtil::setWin(false);
  PgUtil::setBox(false);

  PgUtil::clearTraceColor();

  // For 1D datasets, resample the x-axis to the finest sampling found
  // in the data

  DataSet1D* ds1d = dynamic_cast<DataSet1D*>(ds);
  double xmin  = ds1d->x_[0];
  double xmax  = ds1d->x_[0];
  double dxmin = ds1d->x_[1] - ds1d->x_[0];
  double xcurr, xprev, dx;

  for(unsigned i=0; i < ds1d->x_.size(); i++) {

    xcurr = ds1d->x_[i];

    if(i > 0) {
      dx = xcurr - xprev;
      dxmin = dxmin < dx ? dxmin : dx;
    }

    xmin = xcurr < xmin ? xcurr : xmin;
    xmax = xcurr > xmax ? xcurr : xmax;

    xprev = xcurr;
  }

  dxmin = 0.1;

  unsigned n = (unsigned)ceil(((xmax - xmin) / dxmin));
  //  COUT("xmin = " << xmin << " xmax = " << xmax << "dxmin = " << dxmin << " n = " << n);

  ds1d->x_.resize(n);
  ds1d->compositeModel_.resize(n);

  for(unsigned i=0; i < n; i++) {
    xcurr = xmin + dxmin * i;
    ds1d->x_[i] = xcurr;
  }

  mm_.setValues(mm_.bestFitSample_);
  dm_.clearModel();
  dm_.addModel(mm_);

  PgUtil::setTraceColor(6);
  cpgsvp(xvp1, xvp2, yvp1+dyvp, yvp2);
  cpgswin(xw1, xw2, yw1, yw2);

  ds->displayCompositeModel();
  PgUtil::clearTraceColor();
}

/**.......................................................................
 * Display function for 2D data sets
 */
void displayDataSet2D(DataSet* ds, double xvp1, double xvp2, double yvp1, double yvp2)
{
  mm_.setValues(mm_.bestFitSample_);
  dm_.clearModel();
  dm_.addModel(mm_);

  mm_.printModel("");

  double dxvp = (xvp2 - xvp1)/2;
  double dyvp = (yvp2 - yvp1)/2;

  PgUtil::setWin(false);
  PgUtil::setBox(false);
  PgUtil::setTick(false);
  PgUtil::setLabel(false);
  
  cpgsvp(xvp1, xvp1+dxvp, yvp1+dyvp, yvp2);
  //  cpgwnad(xvp1, xvp1+dxvp, yvp1+dyvp, yvp2);
  ds->display();
  
  cpgsvp(xvp1, xvp1+dxvp, yvp1, yvp1+dyvp);
  //cpgwnad(xvp1, xvp1+dxvp, yvp1, yvp1+dyvp);
  ds->displayCompositeModel();
  
  PgUtil::setTraceColor(2);
  
  cpgsvp(xvp1+dxvp, xvp2, yvp1, yvp1+dyvp);
  //cpgwnad(xvp1+dxvp, xvp2, yvp1, yvp1+dyvp);
  ds->displayResiduals();
  
  PgUtil::clearTraceColor();

  COUTCOLOR(std::endl << dm_.computeChisq(), "green");
}

void updateHessian(Probability startProb)
{
  // Store the current vector of values

  Vector<double> storedVal = mm_.currentSample_;
  Vector<double> mean      = mm_.mean_;
  Vector<double> sigmas    = mm_.sigma_;
  Matrix<double> invCov    = mm_.invCov_;
  double detC              = mm_.detC_;

  unsigned nVar = storedVal.size();
  Vector<double> x0(nVar), x1(nVar), x2(nVar), tmp(nVar);
  double lp0, lp1, lp2, dlp1, dlp2, d2;
  double val1, val2;

  Vector<double> newSigmas = sigmas;
  double newsigval;
  for(unsigned iVar=0; iVar < nVar; iVar++) {

    // Generate a new random point from this variate's conditional
    // distribution

    tmp = storedVal;
    Probability prob = startProb;

    x0 = tmp;
    lp0 = prob.lnValue();

    if(!getNextAcceptedSample(iVar, tmp, mean, invCov, prob)) {
      return;
    }

    x1 = tmp;
    lp1 = prob.lnValue();

    if(!getNextAcceptedSample(iVar, tmp, mean, invCov, prob)) {
      return;
    }

    x2 = tmp;
    lp2 = prob.lnValue();

    // Arrange the samples in increasing order

    val1 = (x1[iVar] + x0[iVar])/2;
    val2 = (x2[iVar] + x1[iVar])/2;

    // Calculate dlogp/dx at each point

    dlp1 = (lp1 - lp0) / (x1[iVar] - x0[iVar]);
    dlp2 = (lp2 - lp1) / (x2[iVar] - x1[iVar]);

    // Finally, calculate d2logp/dx2

    d2 = (dlp2 - dlp1) / (val2 - val1);

    newsigval = 1.0/sqrt(-d2);

    // Can't invert nans

    if(!finite(newsigval)) {
      return;
    }

    newSigmas[iVar] = newsigval;

    COUT("newSigma[" << iVar << "] = " << newSigmas[iVar]);
  }

  COUT("Updating sigmas");

  mm_.setSamplingSigmas(newSigmas);
  mm_.updateSamplingSigmas();
}

bool updateHessian2(Probability startProb)
{
  // Store the current vector of values

  Vector<double> storedVal = mm_.currentSample_;
  Vector<double> mean      = mm_.mean_;
  Vector<double> sigmas    = mm_.sigma_;
  Matrix<double> invCov    = mm_.invCov_;
  double detC              = mm_.detC_;

  unsigned nVar = storedVal.size();
  Vector<double> x0(nVar), x1(nVar), x2(nVar), tmp(nVar);
  double lp0, lp1, lp2, dlp1, dlp2, d2;
  double val1, val2;
  double xlo, xmid, xhi;
  double lplo, lpmid, lphi;

  Vector<double> newSigmas = sigmas;
  double newsigval;
  for(unsigned iVar=0; iVar < nVar; iVar++) {

    // Generate a new random point from this variate's conditional
    // distribution

    tmp = storedVal;
    Probability prob = startProb;

    x0 = tmp;
    lp0 = prob.lnValue();

    if(!getNextAcceptedSample(iVar, tmp, mean, invCov, prob)) {
      return false;
    }

    x1 = tmp;
    lp1 = prob.lnValue();

    if(x0[iVar] < x1[iVar]) {
      xlo  = x0[iVar];
      lplo = lp0;

      xhi  = x1[iVar];
      lphi = lp1;
    } else {
      xlo  = x1[iVar];
      lplo = lp1;

      xhi  = x0[iVar];
      lphi = lp0;
    }

    if(!getNextAcceptedSample(iVar, tmp, mean, invCov, prob)) {
      return false;
    }

    x2  = tmp;
    lp2 = prob.lnValue();

    if(x2[iVar] < xlo) {
      xmid  = xlo;
      lpmid = lplo;

      xlo  = x2[iVar];
      lplo = lp2;
    } else if(x2[iVar] > xhi) {
      xmid  = xhi;
      lpmid = lphi;

      xhi  = x2[iVar];
      lphi = lp2;
    } else {
      xmid  = x2[iVar];
      lpmid = lp2;
    }

    //    COUT("Calculating derivatives: iVar = " << iVar << " xlo = " << xlo << " xmid = " << xmid << " xhi = " << xhi);
    //    COUT("Calculating derivatives: iVar = " << iVar << " lplo = " << lplo << " lpmid = " << lpmid << " lphi = " << lphi);

    val1 = (xmid +  xlo)/2;
    val2 = (xhi  + xmid)/2;

    // Calculate dlogp/dx at each point

    dlp1 = (lpmid -  lplo) / (xmid -  xlo);
    dlp2 = (lphi  - lpmid) / (xhi  - xmid);

    // Finally, calculate d2logp/dx2

    d2 = (dlp2 - dlp1) / (val2 - val1);

    newsigval = 1.0/sqrt(-d2);

    // Can't invert nans

    if(!finite(newsigval)) {
      return false;
    }

    newSigmas[iVar] = newsigval;

    COUT("newSigma[" << iVar << "] = " << newSigmas[iVar]);
  }

  COUT("Updating sigmas");

  mm_.setSamplingSigmas(newSigmas);
  mm_.updateSamplingSigmas();

  return true;
}

/**.......................................................................
 * Version of updateHessian() that samples the surface locally
 * (regardless of probability of sampled points) and updates the mean
 * of the jumping distribution accordingly.
 */
bool updateHessian3(Probability startProb, double frac, double gamma)
{
  // Store the current vector of values

  Vector<double> storedValues = mm_.currentSample_;
  Vector<double> means        = mm_.mean_;
  Vector<double> sigmas       = mm_.sigma_;

  unsigned nVar = storedValues.size();
  double lp0, lp1, lp2, dlp0, dlp1, dlp2, d2, s2;
  double xval1, xval2;
  double xlo, xmid, xhi;
  double lplo, lpmid, lphi;

  Vector<double> newSigmas = sigmas;
  Vector<double> newMeans  = means;
  Vector<double> sample;

  double newsigval;
  double newmeanval;

  //------------------------------------------------------------
  // Iterate over all variates, sampling the likelihood surface at two
  // points about the current value to determine the curvature of the
  // likelihood surface
  //------------------------------------------------------------

  Probability prob;
  double fracIter;

  for(unsigned iVar=0; iVar < nVar; iVar++) {

    double mean  = means[iVar];
    double sigma = sigmas[iVar];

    // For each variate, we start at the last accepted point and its
    // corresponding probability

    sample = storedValues;
    prob   = startProb;

    // Midpoint of the sample will be the starting point

    xmid  = sample[iVar];
    lpmid = prob.lnValue();
    
    // Low sample will be at xmid - frac*sigma.  We iterate in case
    // xmid - frac*sigma happens to walk off the edge of our prior, in
    // which case prob = 0.0.  In this case, we shrink the step size
    // until we get a valid sample with non-zero probability

    fracIter = frac;
    do {
      xlo = xmid - fracIter*sigma;
      sample[iVar] = xlo;
      getNextSample(sample, prob);
      lplo = prob.lnValue();
      fracIter /= 2;
    } while(!(prob > 0.0));

    // High sample will be at xmid + frac*sigma.  We iterate in case
    // xmid + frac*sigma happens to walk off the edge of our prior, in
    // which case prob = 0.0.  In this case, we shrink the step size
    // until we get a valid sample with non-zero probability

    fracIter = frac;
    do {
      xhi = xmid + fracIter*sigma;
      sample[iVar] = xhi;
      getNextSample(sample, prob);
      lphi = prob.lnValue();
      fracIter /= 2;
    } while(!(prob > 0.0));

    //    COUT("iVar = " << iVar << "lplo = " << lplo << " lpmid = " << lpmid << " lphi = " << lphi);

    // Now we have all three samples -- calculate derivatives

    //    COUT("Calculating derivatives: xlo = " << xlo << " xmid = " << xmid << " xhi = " << xhi);

    // X-values at the points where the first derivatives were
    // evaluated

    xval1 = (xmid +  xlo)/2;
    xval2 = (xhi  + xmid)/2;

    // Calculate dlogp/dx at each of these points

    dlp1 = (lpmid -  lplo) / (xmid -  xlo);
    dlp2 = (lphi  - lpmid) / (xhi  - xmid);

    // Estimate dlogp/dx at the midpoint by interpolating between
    // these two

    dlp0 = dlp1 + (dlp2 - dlp1) / (xval2 - xval1) * (xmid - xval1);

    // Finally, calculate d2logp/dx2

    d2 = (dlp2 - dlp1) / (xval2 - xval1);

    //    COUT("iVar = " << iVar << " d2 = " << d2);

    // Estimate of sigma^2 is 1.0/(-d2logp/dx2);

    s2  = -1.0/d2;

    // sigma is sqrt() of that

    newsigval  = sqrt(s2);

    // And the estimated mean of the distribution is offset from the
    // current sample position by the first derivative

    newmeanval = xmid + gamma * dlp0 * s2;

    // Set the mew sampling means and sigmas, but only if we have
    // detected positive curvature (s2 > 0.0)

    if(s2 > 0.0) {
      newSigmas[iVar] = newsigval;
      newMeans[iVar]  = newmeanval;
    }

    //    COUT("oldSigmas[" << iVar << "] = " << sigmas[iVar] << " oldMean[" << iVar << "] = " << means[iVar]);
    //    COUT("newSigmas[" << iVar << "] = " << newSigmas[iVar] << " newMean[" << iVar << "] = " << newMeans[iVar]);
  }

  COUT("Updating means");

  mm_.previousSample_ = newMeans;
  mm_.setSamplingMeans(newMeans);

  if(lpmid > 2 * nVar * -80.0) {
    COUT("Updating sigmas");
    mm_.setSamplingSigmas(newSigmas);
    mm_.updateSamplingSigmas();

    return true;
  }

  return false;
}

/**.......................................................................
 * Sample from the conditional distribution of the specified variate
 * (iVar) until an accepted sample is found
 */
bool getNextAcceptedSample(unsigned iVar, Vector<double>& tmp, Vector<double>& mean, Matrix<double>& invCov, Probability& prob)
{
  Probability rat, propDensCurr, likeCurr;

  // Store the values on entry -- we will reset at the start of each
  // iteration until an accepted draw is found

  Probability storedProb    = prob;
  Vector<double>& storedVal = tmp;

  // Iterate drawing samples from the conditional distribution of the
  // requested variate until one is accepted

  bool accepted = false;
  unsigned ntry=1000, itry=0;

  do {

    tmp = storedVal;
    storedProb = prob;

    double alpha = Sampler::generateUniformSample(0.0, 1.0);
    
    dm_.clearModel();

    mm_.previousSample_ = tmp;
    Sampler::multiVariateSampleIterator(iVar, tmp, mean, invCov);
    mm_.currentSample_ = tmp;
    mm_.setValues(tmp);
    
    propDensCurr = mm_.priorPdf();
    rat = propDensCurr/prob;
    
    if(rat > alpha) {

      likeCurr = dm_.likelihood(mm_);
      rat *= likeCurr;
      
      if(rat > alpha) {
	accepted = true;
	prob = likeCurr*propDensCurr;
	return true;
      } else {
	accepted = false;
	tmp = storedVal;
	prob = storedProb;
	mm_.revert();
      }
    }

    ++itry;
    
  } while(!accepted && itry < ntry);

  return false;
}

/**.......................................................................
 * Calculate the probability of the current sample
 */
void getNextSample(Vector<double>& sample, Probability& prob)
{
  dm_.clearModel();

  mm_.currentSample_  = sample;
  mm_.setValues(sample);
    
  prob = mm_.priorPdf() * dm_.likelihood(mm_);    
}

void computeChisq()
{
  COUT("Inside computeChisq: nVar = " << mm_.nVar());

  mm_.updateVariableMap();

  if(mm_.nVar() > 0) {
    ThrowColorError("I can't compute chisq for a model with variable components", "red");
  }

  dm_.clearModel();
  dm_.addModel(mm_);
  COUT("Chisq = " << dm_.computeChisq() << " for model: ");

  mm_.printModel("");
}

bool isNumeric(char c)
{
  return isdigit(c) || c=='-' || c=='+';
}

/**.......................................................................
 * Take an input value string for a variable assignment, and see if
 * the value is a symbolic name.  If so, get the value of the variable
 * that that symbolic name corresponds to and reeturn that value
 * instead.
 */
String getVariableValueString(String& inputValStr)
{
  String valStr = inputValStr;

  if(valStr.contains(".")) {

    String ownerName, varName;
    parseVarname(valStr, ownerName, varName);
    
    // Now check if the owner is a model or a dataset, or neither
    
    Model*   model   = 0;
    DataSet* dataSet = 0;
    
    try {
      model = mm_.getModel(ownerName.str());
      Variate* var  = mm_.getVar(varName.str());
    } catch(Exception& err) {
    }

    // If this was a valid model.variate specification, return the
    // value of that variable as a (possibly) unit'd string
    
    if(model) {
      Variate* var  = mm_.getVar(varName.str());
      std::ostringstream os;
      os << var->getUnitVal() << " " << var->units();
      valStr = os.str();
      return valStr;
    }
    
    // If we got here, then no model matches -- check datasets
    
    try {
      dataSet = dm_.getDataSet(ownerName.str());
      DataSet::Parameter* par = dataSet->getParameter(varName.str(), true);
    } catch(Exception& err) {
    }
    
    // If this was a valid dataset.parameter specification, return the
    // value of that variable as a (possibly) unit'd string

    if(dataSet) {
      DataSet::Parameter* par = dataSet->getParameter(varName.str(), true);
      std::ostringstream os;
      os << par->data_ << " " << par->units_;
      valStr = os.str();
      return valStr;
    }
  }

  // If we got here, then either the value string doesn't look like a
  // symbolic name, or doesn't resolve to any symbols we know about,
  // so pass it back unmodified

  return inputValStr;
}

/**.......................................................................
 * Return true if this assignmemnt depends on a dataset, in which case
 * we must process it after datasets have been read in.
 */
bool assignmentDependsOnDataset(String& inputVal)
{
  String valStr = inputVal;

  if(valStr.contains(".")) {
    String ownerName, varName;
    parseVarname(valStr, ownerName, varName);

    try {
      DataSet* dataSet = dm_.getDataSet(ownerName.str());
      return true;
    } catch(Exception& err) {
    }
  }

  return false;
}

/**.......................................................................
 * Load any data sets that were specified
 */
void loadData()
{
  dm_.loadData(false);
  datasetsInitialized_ = true;
}

/**.......................................................................
 * Process any lines that depend on datasets having been read in
 */
void processDatasetDependentLines()
{
  for(unsigned iLine=0; iLine < datasetDependentLines_.size(); iLine++) {
    String line(datasetDependentLines_[iLine]);
    processLine(line, 0, 0);
  }
}
