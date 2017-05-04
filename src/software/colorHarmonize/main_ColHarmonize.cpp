
// Copyright (c) 2013, 2014 openMVG authors.

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "software/colorHarmonize/colorHarmonizeEngineGlobal.hpp"

#include "third_party/cmdLine/cmdLine.h"
#include "third_party/stlplus3/filesystemSimplified/file_system.hpp"
#include "openMVG/system/timer.hpp"

#include <cstdlib>
#include <memory>

using namespace openMVG;

int main( int argc, char **argv )
{
  using namespace std;
  std::cout << "Global Color Harmonization" << std::endl
            << std::endl;

  CmdLine cmd;

  std::string sSfM_Data_Filename;
  std::string sMatchesDir;
  std::string describerMethods = "SIFT";
  std::string sMatchesGeometricModel = "f";
  std::string sOutDir = "";
  int selectionMethod = -1;
  int imgRef = -1;

  cmd.add( make_option('i', sSfM_Data_Filename, "input_file" ));
  cmd.add( make_option('m', sMatchesDir, "matchesDir" ));
  cmd.add( make_option('d', describerMethods, "describerMethods") );
  cmd.add( make_option('o', sOutDir, "outdir" ));
  cmd.add( make_option('s', selectionMethod, "selectionMethod" ));
  cmd.add( make_option('r', imgRef, "referenceImage" ));
  cmd.add( make_option('g', sMatchesGeometricModel, "matchesGeometricModel"));

  try
  {
    if( argc == 1 ) throw std::string( "Invalid command line parameter." );
    cmd.process( argc, argv );
  }
  catch( const std::string& s )
  {
    std::cerr << "Usage: " << argv[ 0 ] << ' '
    << "[-i|--input_file] path to the sfm_data.json kind of file generated by OpenMVG"
    << "[-m|--matchesDir path] "
    << "[-d|--describerMethods]\n"
    << "  (methods to use to describe an image):\n"
    << "   SIFT (default),\n"
    << "   SIFT_FLOAT to use SIFT stored as float,\n"
    << "   AKAZE: AKAZE with floating point descriptors,\n"
    << "   AKAZE_MLDB:  AKAZE with binary descriptors\n"
#ifdef HAVE_CCTAG
    << "   CCTAG3: CCTAG markers with 3 crowns\n"
    << "   CCTAG4: CCTAG markers with 4 crowns\n"
#endif
#ifdef HAVE_OPENCV
#ifdef USE_OCVSIFT
    << "   SIFT_OCV: OpenCV SIFT\n"
#endif
    << "   AKAZE_OCV: OpenCV AKAZE\n"
#endif
    << "[-o|--outdir path] "
    << "[-s|--selectionMethod int] "
    << "    - FullFrame: 0"
    << "    - Matched Points: 1"
    << "    - VLD Segment: 2"
    << "[-r|--referenceImage id int]"
    << "\n[Optional]\n"
    << "[-g|--matchesGeometricModel MODEL] matching geometric model used: 'f' (default), 'e' or 'h'"
    << std::endl;

    std::cerr << s << std::endl;
    return EXIT_FAILURE;
  }

  if ( sSfM_Data_Filename.empty() )
  {
    std::cerr << "\nIt is an invalid file input" << std::endl;
    return EXIT_FAILURE;
  }

  const std::vector<features::EImageDescriberType> describerTypes = features::EImageDescriberType_stringToEnums(describerMethods);

  if ( !stlplus::folder_exists( sOutDir ) )
    stlplus::folder_create( sOutDir );

  //---------------------------------------
  // Harmonization process
  //---------------------------------------

  openMVG::system::Timer timer;

  ColorHarmonizationEngineGlobal colorHarmonizeEngine(sSfM_Data_Filename,
    sMatchesDir,
    sMatchesGeometricModel,
    sOutDir,
    describerTypes,
    selectionMethod,
    imgRef);

  if (colorHarmonizeEngine.Process() )
  {
    clock_t timeEnd = clock();
    std::cout << std::endl
      << " ColorHarmonization took (s): "
      << timer.elapsed() << std::endl;

    return EXIT_SUCCESS;
  }
  else
  {
    std::cerr << "\n Something goes wrong in the process" << std::endl;
  }
  return EXIT_FAILURE;
}
