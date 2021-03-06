#include "gcp/matlab/MexHandler.h"
#include "gcp/matlab/MexParser.h"

#include "gcp/util/FitsBinTableReader.h"
#include "gcp/util/String.h"

#include <sstream>

using namespace std;

using namespace gcp::matlab;
using namespace gcp::util;

/**.......................................................................
 * Constructor.
 */
MexHandler::MexHandler() {}

/**.......................................................................
 * Destructor.
 */
MexHandler::~MexHandler() {}

/**.......................................................................
 * Methods for creating matlab arrays
 */
mxArray* MexHandler::createMatlabArray(int ndim, std::vector<int> dims, DataType::Type dataType)
{
  return createMatlabArray(ndim, (const int*)&dims[0], dataType);
}

/**.......................................................................
 * Methods for creating matlab arrays
 */
mxArray* MexHandler::createMatlabArray(int ndim, const int* dims, DataType::Type dataType)
{
  std::vector<mwSize> mtDims = convertDims(ndim, dims);

  switch (dataType) {
  case DataType::DATE:
  case DataType::STRING:
    return mxCreateCellArray((mwSize)ndim, (const mwSize*)&mtDims[0]);
  default:
    return mxCreateNumericArray((mwSize)ndim, (const mwSize*)&mtDims[0], matlabTypeOf(dataType), matlabComplexityOf(dataType));
    break;
  }
}

mxArray* MexHandler::createMatlabArray(int length, DataType::Type dataType)
{
  return createMatlabArray(1, &length, dataType);
}

double* MexHandler::createDoubleArray(mxArray** array, unsigned len)
{
  int dims[1];

  dims[0] = len;

  *array = createMatlabArray(1, dims, DataType::DOUBLE);
  return (double*)mxGetData(*array);
}

double* MexHandler::createDoubleArray(mxArray** array, int ndim, std::vector<int> dims)
{
  return createDoubleArray(array, ndim, (const int*)&dims[0]);
}

double* MexHandler::createDoubleArray(mxArray** array, int ndim, const int* dims)
{
  *array = createMatlabArray(ndim, dims, DataType::DOUBLE);
  return (double*)mxGetData(*array);
}

float* MexHandler::createFloatArray(mxArray** array, int ndim, const int* dims)
{
  *array = createMatlabArray(ndim, dims, DataType::FLOAT);
  return (float*)mxGetData(*array);
}

/**.......................................................................
 * Methods for returning the matlab complexity corresponding to a
 * dataType
 */
mxComplexity MexHandler::matlabComplexityOf(DataType::Type dataType)
{
  switch (dataType) {
  case DataType::COMPLEX_FLOAT:
    return mxCOMPLEX;
    break;
  default:
    return mxREAL;
    break;
  }
}

mxClassID MexHandler::matlabTypeOf(DataType::Type dataType)
{
  switch (dataType) {
  case DataType::BOOL:
  case DataType::UCHAR:
    return mxUINT8_CLASS;
    break;
  case DataType::CHAR:
    return mxINT8_CLASS;
    break;

    // Creating mxUINT16_CLASS arrays crashes matlab!  So I've
    // deliberately redefined this to create 32-bit ints.

  case DataType::USHORT:
    return mxUINT32_CLASS;
    break;
  case DataType::SHORT:
    return mxINT32_CLASS;
    break;
  case DataType::UINT:
    return mxUINT32_CLASS;
    break;
  case DataType::INT:
    return mxINT32_CLASS;
    break;
  case DataType::ULONG:
    return mxUINT64_CLASS;
    break;
  case DataType::LONG:
    return mxINT64_CLASS;
    break;
  case DataType::FLOAT:
    return mxSINGLE_CLASS;
    break;
  case DataType::COMPLEX_FLOAT:
    return mxSINGLE_CLASS;
    break;
  case DataType::DOUBLE:
    return mxDOUBLE_CLASS;
    break;
  }
}

/**.......................................................................
 * Add a named struct field to the passed structure.  
 */
mxArray* MexHandler::addHierNamedStructField(mxArray* parentPtr, 
					     std::string fieldName, 
					     gcp::util::DataType::Type dataType,
					     unsigned nDim, 
					     int* inDims)
{
  String name(fieldName);
  String last, next;

  mxArray* currPtr = parentPtr;
  mxArray* lastPtr = parentPtr;

  int dims[2] = {1,1};

  std::vector<mwSize> mtDims = convertDims(2, dims);

  do {

    next = name.findNextStringSeparatedByChars(".");

    if(!next.isEmpty()) {
  
      // Set the last pointer pointing to the current pointer so that
      // the next level of the structure is added hierarchically to
      // this one
      
      lastPtr = currPtr;
      last    = next;

      // Add this field if it doesn't already exist

      if((currPtr=mxGetField(lastPtr, 0, next.str().c_str()))==NULL) {
    
	mxAddField(lastPtr, next.str().c_str());
    
	// create empty register substructure
    
	currPtr = mxCreateStructArray((mwSize)2,(const mwSize*)&mtDims[0], 0, NULL);
    
	// link it to the parent structure
    
	mxSetField(lastPtr, 0, next.str().c_str(), currPtr);
      }
    }

  } while(!next.isEmpty());

  // If an array was to be allocated to this struct, add it now

  if(dataType != gcp::util::DataType::NONE) {

    std::vector<int> newDims;
    int* dimPtr=inDims;

    if(nDim < 2) {
      nDim = 2;
      newDims.resize(2);
      newDims[0] = 1;

      if(nDim == 0) {
	newDims[1] = 1;
      } else {
	newDims[1] = dimPtr[0];
      }

      dimPtr = &newDims[0];
    }

    mxArray* tmpPtr = createMatlabArray(nDim, dimPtr, dataType);
    mxSetField(lastPtr, 0, last.str().c_str(), tmpPtr);

    lastPtr = tmpPtr;
  }

  // Return a pointer to the last level added

  return lastPtr;
}

/**.......................................................................
 * Add a named struct field to the passed structure
 */
mxArray* MexHandler::addNamedStructField(mxArray* parentPtr, std::string fieldName, unsigned nElement, unsigned index)
{
  int dims[2]={nElement,1};
  
  std::vector<mwSize> mtDims = convertDims(2, dims);

  // Now add a field for the board
  
  mxArray* childPtr=NULL;
  
  if((childPtr=mxGetField(parentPtr, index, fieldName.c_str()))==NULL) {
    
    // Add it
    
    mxAddField(parentPtr, fieldName.c_str());
    
    // create empty register substructure
    
    childPtr = mxCreateStructArray((mwSize)2, (const mwSize*)&mtDims[0], 0, NULL);
    
    // link it to board structure
    
    mxSetField(parentPtr, index, fieldName.c_str(), childPtr);
  }
  
  return childPtr;
}

/**.......................................................................
 * Add a named string field to the passed structure
 */
char* MexHandler::addNamedStringStructField(mxArray* parentPtr, std::string fieldName, unsigned len, unsigned index)
{
  std::string str;
  str.resize(len);

  return addNamedStringStructField(parentPtr, fieldName, str, index);
}

/**.......................................................................
 * Add a named string field to the passed structure
 */
char* MexHandler::addNamedStringStructField(mxArray* parentPtr, std::string fieldName, std::string str, unsigned index)
{
  // Now add a field for the board
  
  mxArray* childPtr=NULL;
  
  if((childPtr=mxGetField(parentPtr, index, fieldName.c_str()))==NULL) {
    
    // Add it
    
    mxAddField(parentPtr, fieldName.c_str());
    
    // create empty register substructure
    
    childPtr = mxCreateString(str.c_str());
    
    // link it to board structure
    
    mxSetField(parentPtr, index, fieldName.c_str(), childPtr);
  }
  
  return (char*)mxGetData(childPtr);
}

/**.......................................................................
 * Add a named field to the passed structure, and create an unsigned
 * array for it
 */
unsigned* 
MexHandler::addNamedUintStructField(mxArray* parentPtr, std::string fieldName,
				      unsigned nElement, unsigned index)
{
  int dims[2] = {nElement, 1};

  addNamedStructField(parentPtr, fieldName);

  mxArray* array = MexHandler::createMatlabArray(2, dims, DataType::UINT);

  mxSetField(parentPtr, index, fieldName.c_str(), array);

  return (unsigned int*)mxGetData(array);
}

/**.......................................................................
 * Add a named field to the passed structure, and create a double
 * array for it
 */
double* 
MexHandler::addNamedDoubleStructField(mxArray* parentPtr, std::string fieldName,
				      unsigned nElement, unsigned index)
{
  int dims[2] = {nElement, 1};

  addNamedStructField(parentPtr, fieldName);

  mxArray* array = MexHandler::createMatlabArray(2, dims, DataType::DOUBLE);

  mxSetField(parentPtr, index, fieldName.c_str(), array);

  return (double*)mxGetData(array);
}

/**.......................................................................
 * Add a named field to the passed structure, and create a double
 * array for it
 */
double* 
MexHandler::addNamedDoubleStructField(mxArray* parentPtr, std::string fieldName,
				      std::vector<int> dims, unsigned index)
{
  addNamedStructField(parentPtr, fieldName);
  mxArray* array = MexHandler::createMatlabArray(dims.size(), &dims[0], DataType::DOUBLE);
  mxSetField(parentPtr, index, fieldName.c_str(), array);
  return (double*)mxGetData(array);
}

/**.......................................................................
 * Add a named field to the passed structure, and create a float
 * array for it
 */
float* 
MexHandler::addNamedFloatStructField(mxArray* parentPtr, std::string fieldName,
				      unsigned nElement, unsigned index)
{
  int dims[2] = {nElement, 1};

  addNamedStructField(parentPtr, fieldName);

  mxArray* array = MexHandler::createMatlabArray(2, dims, DataType::FLOAT);

  mxSetField(parentPtr, index, fieldName.c_str(), array);

  return (float*)mxGetData(array);
}

/**.......................................................................
 * Add a named field to the passed structure, and create a float
 * array for it
 */
float* 
MexHandler::addNamedFloatStructField(mxArray* parentPtr, std::string fieldName,
				      std::vector<int> dims, unsigned index)
{
  addNamedStructField(parentPtr, fieldName);
  mxArray* array = MexHandler::createMatlabArray(dims.size(), &dims[0], DataType::FLOAT);
  mxSetField(parentPtr, index, fieldName.c_str(), array);
  return (float*)mxGetData(array);
}

/**.......................................................................
 * Add a named struct field to the passed structure
 */
mxArray* MexHandler::addNamedCellField(mxArray* parentPtr, std::string fieldName, unsigned iDim, unsigned index)
{
  int dims[2]={1, iDim};
  
  std::vector<mwSize> mtDims = convertDims(2, dims);

  // Now add a field for the board
  
  mxArray* childPtr=NULL;
  
  if((childPtr=mxGetField(parentPtr, index, fieldName.c_str()))==NULL) {
    
    // Add it
    
    mxAddField(parentPtr, fieldName.c_str());
    
    // create empty register substructure
    
    childPtr = mxCreateCellArray((mwSize)2, (const mwSize*)&mtDims[0]);
    
    // link it to board structure
    
    mxSetField(parentPtr, index, fieldName.c_str(), childPtr);
  }
  
  return childPtr;
}

/**.......................................................................
 * Check arguments and complain if they do not match expectations
 */
void MexHandler::checkArgs(int nlhsExpected, int nrhsExpected, 
			   int nlhsActual,   int nrhsActual)
{
  std::ostringstream os;

  // Get the command-line arguments from matlab 
  
  if(nrhsActual != nrhsExpected) {
    os.str("");
    os << "Must have " << nrhsExpected << " input arguments\n";
    mexErrMsgTxt(os.str().c_str());
  }

  if(nlhsActual != nlhsExpected) {
    os.str("");
    os << "Must have " << nlhsExpected << " output arguments\n";
    mexErrMsgTxt(os.str().c_str());
  } 
}

LOG_HANDLER_FN(MexHandler::stdoutPrintFn)
{
  mexPrintf(logStr.c_str());
}

LOG_HANDLER_FN(MexHandler::stderrPrintFn)
{
  mexPrintf(logStr.c_str());
}

ERR_HANDLER_FN(MexHandler::throwFn)
{
  // For some reason, putting this line before the call to
  // mexErrMsgTxt() prevents a seg fault when mexErrMsgTxt() is called!

  std::cout << "" << std::endl;
  mexErrMsgTxt(os.str().c_str());
}

ERR_HANDLER_FN(MexHandler::reportFn)
{
  std::cout << "" << std::endl;
  mexPrintf(os.str().c_str());
}

ERR_HANDLER_FN(MexHandler::logFn)
{
  std::cout << "" << std::endl;
  mexPrintf(os.str().c_str());
}

mxArray* MexHandler::addNamedStructField(mxArray* parentPtr, std::string fieldName, int ndim, std::vector<int>& dims,
					 gcp::util::DataType::Type dataType, unsigned index)
{
  return addNamedStructField(parentPtr, fieldName, ndim, (const int*)&dims[0], dataType, index);
}

mxArray* MexHandler::addNamedStructField(mxArray* parentPtr, std::string fieldName, int ndim, const int* dims, 
					 gcp::util::DataType::Type dataType, unsigned index)
{
  addNamedStructField(parentPtr, fieldName);

  mxArray* array = MexHandler::createMatlabArray(ndim, dims, dataType);

  mxSetField(parentPtr, index, fieldName.c_str(), array);

  return array;
}

/**.......................................................................
 * Add a named field to the passed structure, and create an
 * properly-typed array for it
 */
void* 
MexHandler::addNamedStructField(mxArray* parentPtr, std::string fieldName,
				FitsBinTableReader* reader, unsigned iCol, unsigned index)
{
  addNamedStructField(parentPtr, fieldName);

  mxArray* array = MexHandler::createMatlabArray(reader, iCol);

  mxSetField(parentPtr, index, fieldName.c_str(), array);

  return mxGetData(array);
}

/**.......................................................................
 * Create a matlab array appropriate for the numbered column
 * from a FITS table
 */
mxArray* MexHandler::createMatlabArray(FitsBinTableReader* reader, unsigned colNo)
{
  int dims[2];
  dims[0] = reader->nRow();
  dims[1] = reader->colRepeat(colNo)==0 ? 1 : reader->colRepeat(colNo);
  
  std::vector<mwSize> mtDims;

  switch (reader->colDataType(colNo)) {
  case DataType::STRING:
    mtDims = convertDims(1, dims);
    return mxCreateCellArray(1, (const mwSize*)&mtDims[0]);
    break;
  case DataType::UCHAR:
    mtDims = convertDims(2, dims);
    return mxCreateCharArray(2, (const mwSize*)&mtDims[0]);
    break;
  case DataType::BOOL:
    mtDims = convertDims(2, dims);
    return mxCreateLogicalArray(2, (const mwSize*)&mtDims[0]);
    break;
  default:
    mtDims = convertDims(2, dims);
    return mxCreateNumericArray(2, (const mwSize*)&mtDims[0],
				matlabTypeOf(reader->colDataType(colNo)),
				matlabComplexityOf(reader->colDataType(colNo)));
    break;
  }
}

/**.......................................................................
 * Add a named struct field that has the same dimensions and
 * type of the field named in copyName
 */
double* MexHandler::copyNamedDoubleStructField(mxArray* parentPtr, std::string fieldName, std::string copyName)
{
  const mxArray* fldPtr = MexParser::getField(parentPtr, copyName);
  const int ndim = MexParser::getNumberOfDimensions(fldPtr);

  std::vector<int> dims = MexParser::getDimensions(fldPtr);

  return (double*)mxGetData(addNamedStructField(parentPtr, fieldName, ndim, dims, DataType::DOUBLE, 0));
}

/**.......................................................................
 * Utility functions for calculating "multi-dimensional" array
 * indices into a one-dimensional representation.
 */

/**.......................................................................
 * Assumes that first index is the slowest-changing index (matlab-style)
 */
unsigned MexHandler::indexStartingWithSlowest(std::vector<unsigned>& coord, 
					      std::vector<unsigned>& dims)
{
  unsigned index=coord[coord.size()-1];
  for(int i=coord.size()-2; i >=0; i--)
    index = dims[i]*index + coord[i];

  return index;
}

/**.......................................................................
 * Assumes that first index is the fastest-changing index (C style)
 */
unsigned MexHandler::indexStartingWithFastest(std::vector<unsigned>& coord, 
					      std::vector<unsigned>& dims)
{
  unsigned index=coord[0];
  for(int i=1; i < coord.size(); i++)
    index = dims[i]*index + coord[i];

  return index;
}

/**.......................................................................
 * Return an array of C-order idices for the input dimensions.
 * Assumes dims is in order from fastest to slowest changing indices
 */
void MexHandler::getIndicesC(std::vector<unsigned>& cVec, 
			     std::vector<unsigned>& cDims)
{
  std::vector<unsigned> mVec;
  getIndices(cVec, mVec, cDims);
}

/**.......................................................................
 * Return an array of C-order idices for the input dimensions
 * Assumes dims is in order from slowest to fastest changing indices
 */
void MexHandler::getIndicesMatlab(std::vector<unsigned>& mVec, 
				  std::vector<unsigned>& mDims)
{
  std::vector<unsigned> cVec;

  // Invert the order of the matlab dimension array for input to the
  // getIndices() routine, which assumes C-style dimension ordering

  std::vector<unsigned> cDims(mDims.size());
  int iC=0, iM=mDims.size()-1;
  for(; iM >= 0; iM--, iC++) {
    cDims[iC] = mDims[iM];
  }
  
  getIndices(cVec, mVec, cDims);
}

/**.......................................................................
 * Return an array of C-order idices for the input dimensions
 * Assumes dims is in order from slowest to fastest changing indices
 */
void MexHandler::getIndicesMatlab(std::vector<unsigned>& mVec, 
				  std::vector<int>& mDims)
{
  std::vector<unsigned> cVec;

  // Invert the order of the matlab dimension array for input to the
  // getIndices() routine, which assumes C-style dimension ordering

  std::vector<unsigned> cDims(mDims.size());
  int iC=0, iM=mDims.size()-1;
  for(; iM >= 0; iM--, iC++) {
    cDims[iC] = mDims[iM];
  }
  
  getIndices(cVec, mVec, cDims);
}

/**.......................................................................
 * Return an array of C-order and Matlab-order idices for the input
 * dimensions
 */
void MexHandler::getIndices(std::vector<unsigned>& cVec, std::vector<unsigned>& mVec,
			    std::vector<unsigned>& dims, 
			    int iDim, unsigned indLast, unsigned nLast, unsigned baseLast)
{
  unsigned base=0;
  static unsigned iVecInd=0;
  static std::vector<unsigned> coord;

  if(iDim == 0) {
    unsigned n=1;
    for(unsigned i=0; i < dims.size(); i++)
      n *= dims[i];
    cVec.resize(n);
    mVec.resize(n);
    coord.resize(dims.size());
    iVecInd = 0;
  }

  if(iDim <= dims.size()) {
    base = baseLast*nLast + indLast;

    if(iDim > 0) 
      coord[iDim-1] = indLast;

  }

  if(iDim == dims.size()) {
    cVec[iVecInd] = base;
    mVec[iVecInd] = indexStartingWithSlowest(coord, dims);
    iVecInd++;
  }

  if(iDim < dims.size()) {
    for(unsigned indCurr=0; indCurr < dims[iDim]; indCurr++) {
      getIndices(cVec, mVec, dims, iDim+1, indCurr, iDim==0 ? 0 : dims[iDim], base);
    }
  }

  return;
}

unsigned MexHandler::getMatlabIndex(unsigned n1, unsigned n2, 
				    unsigned i1, unsigned i2)
{
  return i2 * n1 + i1;
}

unsigned MexHandler::getMatlabIndex(unsigned n1, unsigned n2, unsigned n3, 
				    unsigned i1, unsigned i2, unsigned i3)
{
  return i3 * (n1 * n2) + i2 * (n1) + i1;
}

double* MexHandler::createDoubleArray(mxArray** mxArray, MexParser& parser)
{
  std::vector<int> dims = parser.getDimensions();

  return createDoubleArray(mxArray, 
			   parser.getNumberOfDimensions(), 
			   dims);
}

std::vector<mwSize> MexHandler::convertDims(int ndim, const int* dims)
{
  std::vector<mwSize> mtDims;
  mtDims.resize(ndim);

  for(unsigned i=0; i < ndim; i++) {
    mtDims[i] = dims[i];
  }

  return mtDims;
}

