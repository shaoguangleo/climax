#include "gcp/util/Debug.h"

#include "gcp/util/PtSrcTiler.h"
#include "gcp/util/Declination.h"
#include "gcp/util/HourAngle.h"
#include "gcp/matlab/MexHandler.h"
#include "gcp/matlab/MexParser.h"

#include "mex.h"
#include "matrix.h"

#include <iostream.h>
#include <math.h>

using namespace std;
using namespace gcp::util;
using namespace gcp::matlab;

/**.......................................................................
 * Entry point from the matlab environment
 */
void mexFunction(int nlhs, mxArray      *plhs[],
		 int nrhs, const mxArray *prhs[])
{
  gcp::util::Logger::installStdoutPrintFn(MexHandler::stdoutPrintFn);
  gcp::util::Logger::installStderrPrintFn(MexHandler::stderrPrintFn);
  
  gcp::util::ErrHandler::installThrowFn(MexHandler::throwFn);
  gcp::util::ErrHandler::installReportFn(MexHandler::reportFn);
  gcp::util::ErrHandler::installLogFn(MexHandler::logFn);
  

  HourAngle ra0;
  ra0.setHours(*(MexParser::getDoubleData(prhs[0])));

  Declination dec0;
  dec0.setDegrees(*(MexParser::getDoubleData(prhs[1])));

  double* fieldRadPtr = MexParser::getDoubleData(prhs[2]);
  double* totalRadPtr = MexParser::getDoubleData(prhs[3]);

  Angle fieldRad;
  fieldRad.setDegrees(*fieldRadPtr);

  Angle totalRad;
  totalRad.setDegrees(*totalRadPtr);
  
  HourAngle ra;
  Declination dec;

  std::vector<PtSrcTiler::Field> fields = PtSrcTiler::constructFields(ra0, dec0, fieldRad, totalRad);

  int mdims[1] = {fields.size()};
  plhs[0] = MexHandler::createMatlabArray(1, mdims, DataType::DOUBLE);
  plhs[1] = MexHandler::createMatlabArray(1, mdims, DataType::DOUBLE);

  double* raPtr = (double*)mxGetData(plhs[0]);
  double* decPtr = (double*)mxGetData(plhs[1]);

  for(unsigned i=0; i < fields.size(); i++) {
    *(raPtr+i)  = fields[i].ra_.hours();
    *(decPtr+i) = fields[i].dec_.degrees();
  }

  return;
}

