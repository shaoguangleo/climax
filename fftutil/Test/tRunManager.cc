#include <iostream>
#include <fstream>
#include <sstream>
#include <math.h>

#include "gcp/program/Program.h"

#include "gcp/fftutil/RunManager.h"
#include "gcp/util/Logo.h"

using namespace gcp::program;
using namespace gcp::util;

KeyTabEntry Program::keywords[] = {
  { "file",           "",  "s", "FITS file to read in"},
  { "nburn",      "1000",  "i", "Number of burn-in samples"},
  { "ntry",      "10000",  "i", "Number of trials"},
  { "nmodelthread",  "0",  "i", "Number of threads in the model pool"},
  { "ndatathread",   "0",  "i", "Number of threads in the data pool"},
  { "cpus",        "any",  "s", "List of CPUs to partition threads among"},

  { END_OF_KEYWORDS,END_OF_KEYWORDS,END_OF_KEYWORDS,END_OF_KEYWORDS},
};

void Program::initializeUsage() 
{
  std::ostringstream os;
  os << "This program is controlled by a text-based run file, specified with the command-line 'file' option.  " << std::endl
     << "Create a runfile with the line 'help;' in it, and run this program on it for more information";

  usage_ = os.str();
};

/**.......................................................................
 * Main -- parse a config file and execute its instructions
 */
int Program::main()
{
  if(!Program::hasValue("file"))
    return 1;

  Logo logo;
  logo.display();

  std::string file      = Program::getStringParameter("file");
  unsigned nTry         = Program::getIntegerParameter("ntry");
  unsigned nBurn        = Program::getIntegerParameter("nburn");
  unsigned nModelThread = Program::getIntegerParameter("nmodelthread");
  unsigned nDataThread  = Program::getIntegerParameter("ndatathread");
 
  RunManager rm;

  rm.setRunFile(file);
  rm.setNtry(nTry);
  rm.setNburn(nBurn);
  rm.setNModelThread(nModelThread);
  rm.setNDataThread(nDataThread);

  rm.run();

  return 0;
}
