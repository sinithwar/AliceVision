
// Copyright (c) 2014, 2015 Pierre MOULON.

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "openMVG/multiview/solver_homography_kernel.hpp"
#include "openMVG/robust_estimation/robust_estimator_ACRansac.hpp"
#include "openMVG/robust_estimation/robust_estimator_ACRansacKernelAdaptator.hpp"
#include "openMVG/robust_estimation/guided_matching.hpp"

#include "openMVG/matching/indMatch.hpp"
#include "openMVG/matching/indMatchDecoratorXY.hpp"
#include "openMVG/sfm/sfm_data.hpp"
#include "openMVG/features/RegionsPerView.hpp"
#include "openMVG/matching_image_collection/Geometric_Filter_utils.hpp"

namespace openMVG {
namespace matching_image_collection {

//-- A contrario homography matrix estimation template functor used for filter pair of putative correspondences
struct GeometricFilter_HMatrix_AC
{
  GeometricFilter_HMatrix_AC(
    double dPrecision = std::numeric_limits<double>::infinity(),
    size_t iteration = 1024)
    : m_dPrecision(dPrecision), m_stIteration(iteration), m_H(Mat3::Identity()),
      m_dPrecision_robust(std::numeric_limits<double>::infinity()){};

  /**
   * @brief Given two sets of image points, it estimates the homography matrix
   * relating them using a robust method (like A Contrario Ransac).
   */
  template<typename Regions_or_Features_ProviderT>
  bool Robust_estimation(
    const sfm::SfM_Data * sfmData,
    const Regions_or_Features_ProviderT& regionsPerView,
    const Pair pairIndex,
    const matching::MatchesPerDescType & putativeMatchesPerType,
    matching::MatchesPerDescType & geometricInliersPerType)
  {
    using namespace openMVG;
    using namespace openMVG::robust;
    geometricInliersPerType.clear();

    // Get back corresponding view index
    const IndexT iIndex = pairIndex.first;
    const IndexT jIndex = pairIndex.second;

    const std::vector<features::EImageDescriberType> descTypes = regionsPerView.getCommonDescTypes(pairIndex);
    if(descTypes.empty())
      return false;

    // Retrieve all 2D features as undistorted positions into flat arrays
    Mat xI, xJ;
    MatchesPairToMat(pairIndex, putativeMatchesPerType, sfmData, regionsPerView, descTypes, xI, xJ);

    // Define the AContrario adapted Homography matrix solver
    typedef ACKernelAdaptor<
        openMVG::homography::kernel::FourPointSolver,
        openMVG::homography::kernel::AsymmetricError,
        UnnormalizerI,
        Mat3>
        KernelType;

    KernelType kernel(
      xI, sfmData->GetViews().at(iIndex)->ui_width, sfmData->GetViews().at(iIndex)->ui_height,
      xJ, sfmData->GetViews().at(jIndex)->ui_width, sfmData->GetViews().at(jIndex)->ui_height,
      false); // configure as point to point error model.

    // Robustly estimate the Homography matrix with A Contrario ransac
    const double upper_bound_precision = Square(m_dPrecision);

    std::vector<size_t> inliers;
    const std::pair<double,double> ACRansacOut = ACRANSAC(kernel, inliers, m_stIteration, &m_H, upper_bound_precision);

    if (inliers.size() <= KernelType::MINIMUM_SAMPLES * OPENMVG_MINIMUM_SAMPLES_COEF)
    {
      inliers.clear();
      return false;
    }

    m_dPrecision_robust = ACRansacOut.first;

    // Fill geometricInliersPerType with inliers from putativeMatchesPerType
    copyInlierMatches(
          inliers,
          putativeMatchesPerType,
          descTypes,
          geometricInliersPerType);

    return true;
  }

  /**
   * @brief Export point feature based vector to a matrix [(x,y)'T, (x,y)'T].
   * Use the camera intrinsics in order to get undistorted pixel coordinates
   */
  template<typename MatT >
  static void fillMatricesWithUndistortFeatures(
    const cameras::IntrinsicBase * cam,
    const features::PointFeatures & vec_feats,
    MatT & m)
  {
    using Scalar = typename MatT::Scalar; // Output matrix type

    const bool hasValidIntrinsics = cam && cam->isValid();
    size_t i = 0;

    if (hasValidIntrinsics)
    {
      for( features::PointFeatures::const_iterator iter = vec_feats.begin();
        iter != vec_feats.end(); ++iter, ++i)
      {
          m.col(i) = cam->get_ud_pixel(Vec2(iter->x(), iter->y()));
      }
    }
    else
    {
      for( features::PointFeatures::const_iterator iter = vec_feats.begin();
        iter != vec_feats.end(); ++iter, ++i)
      {
          m.col(i) = iter->coords().cast<Scalar>();
        }
    }
  }

  template<typename MatT >
  static void createMatricesWithUndistortFeatures(
    const cameras::IntrinsicBase * cam,
    const features::MapRegionsPerDesc & regionsPerDesc,
    MatT & m)
  {
    size_t nbRegions = 0;
    for(const auto regions: regionsPerDesc)
    {
      nbRegions += regions.second->RegionCount();
    }
    m.resize(2, nbRegions);

    size_t y = 0;
    for(const auto regions: regionsPerDesc)
    {
      fillMatricesWithUndistortFeatures(
            cam,
            regions.second,
            m.block(0, y, 2, regions.second->RegionCount()));
      y += regions.second->RegionCount();
    }
  }

  template<typename MatT >
  static void createMatricesWithUndistortFeatures(
    const cameras::IntrinsicBase * cam,
    const features::PointFeatures & vec_feats,
    MatT & m)
  {
    size_t nbRegions = vec_feats.size();
    m.resize(2, nbRegions);

    fillMatricesWithUndistortFeatures(
          cam,
          vec_feats,
          m);
  }

  /**
   * @brief Geometry_guided_matching
   * @param sfm_data
   * @param regionsPerView
   * @param pairIndex
   * @param dDistanceRatio
   * @param matches
   * @return
   */
  bool Geometry_guided_matching
  (
    const sfm::SfM_Data * sfmData,
    const features::RegionsPerView& regionsPerView,
    const Pair imageIdsPair,
    const double dDistanceRatio,
    matching::MatchesPerDescType & matches
  )
  {
    if (m_dPrecision_robust != std::numeric_limits<double>::infinity())
    {
      const std::vector<features::EImageDescriberType> descTypes = regionsPerView.getCommonDescTypes(imageIdsPair);
      if(descTypes.empty())
        return false;

      // Get back corresponding view index
      const IndexT viewId_I = imageIdsPair.first;
      const IndexT viewId_J = imageIdsPair.second;

      const sfm::View * view_I = sfmData->views.at(viewId_I).get();
      const sfm::View * view_J = sfmData->views.at(viewId_J).get();

      // Retrieve corresponding pair camera intrinsic if any
      const cameras::IntrinsicBase * cam_I =
        sfmData->GetIntrinsics().count(view_I->id_intrinsic) ?
          sfmData->GetIntrinsics().at(view_I->id_intrinsic).get() : nullptr;
      const cameras::IntrinsicBase * cam_J =
        sfmData->GetIntrinsics().count(view_J->id_intrinsic) ?
          sfmData->GetIntrinsics().at(view_J->id_intrinsic).get() : nullptr;

      if (dDistanceRatio < 0)
      {
        for(const features::EImageDescriberType descType: descTypes)
        {
          matching::IndMatches localMatches;

          const features::Regions& regions_I = regionsPerView.getRegions(viewId_I, descType);
          const features::Regions& regions_J = regionsPerView.getRegions(viewId_J, descType);
          const features::PointFeatures pointsFeaturesI = regions_I.GetRegionsPositions();
          const features::PointFeatures pointsFeaturesJ = regions_J.GetRegionsPositions();

          // Filtering based only on region positions
          Mat xI, xJ;
          createMatricesWithUndistortFeatures(cam_I, pointsFeaturesI, xI);
          createMatricesWithUndistortFeatures(cam_J, pointsFeaturesJ, xJ);

          geometry_aware::GuidedMatching
            <Mat3, openMVG::homography::kernel::AsymmetricError>(
            m_H, xI, xJ, Square(m_dPrecision_robust), localMatches);

          // Remove matches that have the same (X,Y) coordinates
          matching::IndMatchDecorator<float> matchDeduplicator(localMatches, pointsFeaturesI, pointsFeaturesJ);
          matchDeduplicator.getDeduplicated(localMatches);
          matches[descType] = localMatches;
        }
      }
      else
      {
        // Filtering based on region positions and regions descriptors
        geometry_aware::GuidedMatching
          <Mat3, openMVG::homography::kernel::AsymmetricError>(
          m_H,
          cam_I, regionsPerView.getAllRegions(viewId_I),
          cam_J, regionsPerView.getAllRegions(viewId_J),
          Square(m_dPrecision_robust), Square(dDistanceRatio),
          matches);
      }
    }
    return matches.getNbAllMatches() != 0;
  }

  double m_dPrecision;  //upper_bound precision used for robust estimation
  size_t m_stIteration; //maximal number of iteration for robust estimation
  //
  //-- Stored data
  Mat3 m_H;
  double m_dPrecision_robust;
};

} // namespace openMVG
} //namespace matching_image_collection


