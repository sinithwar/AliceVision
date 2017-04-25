
// Copyright (c) 2015 Pierre MOULON.

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "openMVG/sfm/sfm.hpp"
#include "openMVG/sfm/pipelines/RegionsIO.hpp"
#include "openMVG/system/timer.hpp"
#include "openMVG/features/ImageDescriberCommon.hpp"
#include "openMVG/features/regions_factory.hpp"
#include "openMVG/features/svgVisualization.hpp"
#include "openMVG/features/cctag/CCTAG_describer.hpp"
#include "openMVG/matching/indMatch.hpp"

#include "boost/filesystem.hpp"
#include "third_party/cmdLine/cmdLine.h"
#include "third_party/stlplus3/filesystemSimplified/file_system.hpp"

using namespace openMVG;
using namespace openMVG::sfm;
using namespace openMVG::features;

namespace bfs = boost::filesystem;

std::ostream& operator<<(std::ostream& stream, const std::set<IndexT>& s)
{
  stream << "{";
  for(IndexT i: s)
    stream << i << ", ";
  stream << "}" << std::endl;
  return stream;
}

/// Compute the structure of a scene according existing camera poses.
int main(int argc, char **argv)
{
  using namespace std;
  std::cout << "Compute CCTag Structure from the provided poses" << std::endl;

  CmdLine cmd;
  std::string sSfM_Data_Filename;
  std::string describerMethod = "CCTAG3";
  std::string sMatchesDir;
  std::string sOutFile;
  std::string sDebugOutputDir;
  bool sUseSfmVisibility = false;
  bool sKeepSift = false;

  cmd.add( make_option('i', sSfM_Data_Filename, "input_file") );
  cmd.add( make_option('M', describerMethod, "describerMethod") );
  cmd.add( make_option('m', sMatchesDir, "match_dir") );
  cmd.add( make_option('o', sOutFile, "output_file") );
  cmd.add( make_option('s', sKeepSift, "keep_sift") );
  cmd.add( make_option('r', sUseSfmVisibility, "use_sfm_visibility") );
  cmd.add( make_option('d', sDebugOutputDir, "debug_dir") );

  try {
    if (argc == 1) throw std::string("Invalid command line parameter.");
    cmd.process(argc, argv);
  } catch(const std::string& s) {
    std::cerr << "Usage: " << argv[0] << '\n'
    << "[-i|--input_file] path to a SfM_Data scene\n"
    << "[-M|--describerMethod]\n"
    << "  (methods to use to describe an image):\n"
#ifdef HAVE_CCTAG
    << "   CCTAG3: CCTAG markers with 3 crowns\n"
    << "   CCTAG4: CCTAG markers with 4 crowns\n"
    << "   SIFT_CCTAG3: CCTAG markers with 3 crowns\n" 
    << "   SIFT_CCTAG4: CCTAG markers with 4 crowns\n" 
#endif
    << "[-m|--match_dir] path to the features and descriptor that "
    << " corresponds to the provided SfM_Data scene\n"
    << "[-f|--match_file] (opt.) path to a matches file (used pairs will be used)\n"
    << "[-o|--output_file] file where the output data will be stored\n"
    << "[-s|--keep_sift] keep SIFT points (default false)\n"
    << "[-r|--use_sfm_visibility] Use connections between views based on SfM observations instead of relying on frustums intersections (default false)\n"
    << "[-d|--debug_dir] debug output directory to generate svg files with detected CCTags (default \"\")\n"
    << std::endl;

    std::cerr << s << std::endl;
    return EXIT_FAILURE;
  }

  // Load input SfM_Data scene
  SfM_Data reconstructionSfmData;
  if (!Load(reconstructionSfmData, sSfM_Data_Filename, ESfM_Data::ALL)) {
    std::cerr << std::endl
      << "The input SfM_Data file \""<< sSfM_Data_Filename << "\" cannot be read." << std::endl;
    return EXIT_FAILURE;
  }

  using namespace openMVG::features;
  
  // Get imageDescriberMethodType
  EImageDescriberType describerMethodType = EImageDescriberType_stringToEnum(describerMethod);
  
  if((describerMethodType != EImageDescriberType::CCTAG3) &&
    (describerMethodType != EImageDescriberType::CCTAG4) &&
    (describerMethodType != EImageDescriberType::SIFT_CCTAG3) &&
    (describerMethodType != EImageDescriberType::SIFT_CCTAG4))
  {
    std::cerr << "Invalid describer method." << std::endl;
    return EXIT_FAILURE;
  }

  // Prepare the Regions provider
  RegionsPerView regionsPerView;
  if (!sfm::loadRegionsPerView(regionsPerView, reconstructionSfmData, sMatchesDir, {describerMethodType}))
  {
    std::cerr << std::endl
      << "Invalid regions." << std::endl;
    return EXIT_FAILURE;
  }

  //--
  //- Pair selection method
  //  - geometry guided -> camera frustum intersection
  //  - putative matches guided (photometric matches) (Keep pair that have valid Intrinsic & Pose ids)
  //--
  std::cout << "Compute connected views by frustrum intersection." << std::endl;
  std::map<IndexT, std::set<IndexT>> connectedViews;
  {
    Pair_Set viewPairs;
    if (sUseSfmVisibility)
    {
      matching::PairwiseMatches matches;
      if (!matching::Load(matches, reconstructionSfmData.GetViewsKeys(), sMatchesDir, {describerMethodType}, "f"))
      {
        std::cerr<< "Unable to read the matches file." << std::endl;
        return EXIT_FAILURE;
      }
      viewPairs = matching::getImagePairs(matches);

      // Keep only Pairs that belong to valid view indexes.
      viewPairs = Pair_filter(viewPairs, Get_Valid_Views(reconstructionSfmData));
    }
    else
    {
      // No image pair provided, so we use cameras frustum intersection.
      // Build the list of connected images pairs from frustum intersections
      viewPairs = Frustum_Filter(reconstructionSfmData).getFrustumIntersectionPairs();
    }
    
    // Convert pair to map
    for(auto& p: viewPairs)
    {
      connectedViews[p.first].insert(p.second);
      connectedViews[p.second].insert(p.first);
    }
  }
  
  std::cout << "Database of all CCTags" << std::endl;
  // Database of all CCTags: <CCTagId, set<ViewID>>
  std::map<IndexT, std::set<IndexT>> cctagsVisibility;
  // Database of all CCTag observations: <(CCTagId, ViewID), Observation>
  std::map<std::pair<IndexT, IndexT>, Observation> cctagsObservations;
  {
    // List all CCTags in descriptors of all reconstructed cameras
    for(const auto& regionForView: regionsPerView.getData())
    {
      View* view = reconstructionSfmData.GetViews().at(regionForView.first).get();
      if (!reconstructionSfmData.IsPoseAndIntrinsicDefined(view))
      {
        // Consider only reconstructed cameras
        std::cout << "Ignore unreconstructed view (viewId: " << view->id_view << ", poseId: " << view->id_pose << ")" << std::endl;
        continue;
      }
      const features::Regions* regions = regionForView.second.at(describerMethodType).get();
      const features::SIFT_Regions* siftRegions = dynamic_cast<const features::SIFT_Regions*>(regions);
      if(siftRegions == nullptr)
      {
        throw std::runtime_error("Only works with SIFT regions in input.");
      }
      features::SIFT_Regions cctagRegions_debug;
      for(std::size_t i = 0; i < siftRegions->RegionCount(); ++i)
      {
        const features::SIFT_Regions::DescriptorT& cctagDesc = siftRegions->Descriptors()[i];
        IndexT cctagId = features::getCCTagId<features::SIFT_Regions::DescriptorT>(cctagDesc);
        
        if(cctagId == UndefinedIndexT)
          // Not a CCTag
          continue;

        cctagsVisibility[cctagId].insert(regionForView.first);
        cctagsObservations[std::make_pair(cctagId, regionForView.first)] = Observation(siftRegions->Features()[i].coords().cast<double>(), i);
        
        if(!sDebugOutputDir.empty())
        {
          cctagRegions_debug.Features().push_back(siftRegions->Features()[i]);
          cctagRegions_debug.Descriptors().push_back(cctagDesc);
        }
      }
      // DEBUG: export svg files
      if(!sDebugOutputDir.empty())
      {
        cameras::IntrinsicBase* intrinsics = reconstructionSfmData.GetIntrinsics().at(view->id_intrinsic).get();
        features::saveCCTag2SVG(view->s_Img_path, 
                std::make_pair(intrinsics->w(), intrinsics->h()),
                cctagRegions_debug,
                (bfs::path(sDebugOutputDir) / bfs::path(bfs::path(view->s_Img_path).stem().string()+".svg")).string());
      }
    }
  }

  std::cout << "Convert list of all CCTag into landmarks" << std::endl;
  // Convert list of all CCTag into landmarks.
  // The same CCTag ID could be used at different places, so we check frustum intersection
  // to determine the group of CCTag visibility.
  SfM_Data cctagSfmData;
  cctagSfmData.views = reconstructionSfmData.views;
  cctagSfmData.intrinsics = reconstructionSfmData.intrinsics;
  cctagSfmData.poses = reconstructionSfmData.poses;

  IndexT landmarkMaxIndex = 0;
  if(sKeepSift)
  {
    // Ensure we will not reuse the same landmark ID
    for(const auto& landmarkById: reconstructionSfmData.GetLandmarks())
    {
      landmarkMaxIndex = std::max(landmarkById.first+1, landmarkMaxIndex);
    }
  }

  IndexT landmarkIndex = landmarkMaxIndex;
  for(const auto& cctagVisibility: cctagsVisibility)
  {
    std::set<IndexT> viewsWithSameCCTagId = cctagVisibility.second;
    // split groups of cctags with the same ID
    while(!viewsWithSameCCTagId.empty())
    {
      IndexT obsViewId = *viewsWithSameCCTagId.begin();

      std::set<IndexT> cctagSubGroup;
      cctagSubGroup.insert(obsViewId);
      // cctagSubGroup = intersection(connectedViews[obsViewId], cctagsWithSameId)
      std::set_intersection(
        connectedViews[obsViewId].begin(), connectedViews[obsViewId].end(),
        viewsWithSameCCTagId.begin(), viewsWithSameCCTagId.end(),
        std::inserter(cctagSubGroup, cctagSubGroup.end()));
      
      if(cctagSubGroup.size() > 1)
      {
        // Create a new landmark for this CCTag subgroup
        Landmark& landmark = cctagSfmData.structure[landmarkIndex];
        // landmark.X; keep default value, will be set by triangulation in the next step.
        for(IndexT iObsViewId: cctagSubGroup)
        {
          landmark.observations[iObsViewId] = cctagsObservations[std::make_pair(cctagVisibility.first, iObsViewId)];
        }
        ++landmarkIndex;
      }
      // Remove the subgroup from the main one
      for(IndexT i: cctagSubGroup)
        viewsWithSameCCTagId.erase(i);
    }
  }
  
  openMVG::system::Timer timer;

  //------------------------------------------
  // Compute Structure from known camera poses
  //------------------------------------------
  std::cout << "Compute Structure from known camera poses" << std::endl;

  std::cout << "#CCTag nb input IDs used: " << cctagsVisibility.size() << std::endl;
  const std::size_t cctagLandmarkCandidatesSize = cctagSfmData.structure.size();
  std::cout << "#CCTag landmark candidates: " << cctagLandmarkCandidatesSize << std::endl;
  // Triangulate them using a blind triangulation scheme
  SfM_Data_Structure_Computation_Robust structure_estimator(true);
  structure_estimator.triangulate(cctagSfmData);
  const std::size_t cctagReconstructedLandmarksSize = cctagSfmData.structure.size();
  std::cout << "#CCTag landmark reconstructed: " << cctagReconstructedLandmarksSize << std::endl;
  RemoveOutliers_AngleError(cctagSfmData, 2.0);
  std::cout << "#CCTag landmark found: " << cctagSfmData.GetLandmarks().size() << std::endl;

  std::cout << "\nCCTag Structure estimation took (s): " << timer.elapsed() << "." << std::endl;

  if(sKeepSift)
  {
    // Copy non-CCTag landmarks
    for(const auto& landmarkById: reconstructionSfmData.GetLandmarks())
    {
      const auto firstObs = landmarkById.second.observations.cbegin();
      const features::Regions& regions = regionsPerView.getRegions(firstObs->first, describerMethodType);
      const features::SIFT_Regions* siftRegions = nullptr;
      try {
        siftRegions = &dynamic_cast<const SIFT_Regions&>(regions);
      }
      catch(const std::bad_cast& e) {
        throw std::runtime_error("Only works with SIFT regions in input.");
      }

      const SIFT_Regions::DescriptorT& desc = siftRegions->Descriptors()[firstObs->second.id_feat];
      IndexT cctagId = getCCTagId<SIFT_Regions::DescriptorT>(desc);
      if(cctagId != UndefinedIndexT)
        // It's a CCTag, so we ignore it and use the newly triangulated CCTags.
        continue;

      // std::cout << "Add SIFT landmark " << landmarkById.first << std::endl;
      cctagSfmData.structure[landmarkById.first] = landmarkById.second;
    }
  }
  
  if (stlplus::extension_part(sOutFile) != "ply")
  {
    Save(cctagSfmData,
      stlplus::create_filespec(
        stlplus::folder_part(sOutFile),
        stlplus::basename_part(sOutFile), "ply"),
      ESfM_Data(ALL));
  }

  if (Save(cctagSfmData, sOutFile, ESfM_Data(ALL)))
    return EXIT_SUCCESS;
  return EXIT_FAILURE;
}
