// This file is part of the AliceVision project and is made available under
// the terms of the MPL2 license (see the COPYING.md file).

#include "aliceVision/sfm/sfm.hpp"
#include "aliceVision/image/image.hpp"
#include "aliceVision/feature/feature.hpp"
#include "aliceVision/feature/sift/ImageDescriber_SIFT.hpp"
#include "aliceVision/matching/RegionsMatcher.hpp"
#include "aliceVision/multiview/triangulation/triangulationDLT.hpp"

#include "dependencies/stlplus3/filesystemSimplified/file_system.hpp"
#include "dependencies/vectorGraphics/svgDrawer.hpp"

#include <string>
#include <iostream>

using namespace aliceVision;
using namespace aliceVision::matching;
using namespace aliceVision::image;
using namespace aliceVision::camera;
using namespace aliceVision::geometry;
using namespace svg;
using namespace std;

/// Read intrinsic K matrix from a file (ASCII)
/// F 0 ppx
/// 0 F ppy
/// 0 0 1
bool readIntrinsic(const std::string & fileName, Mat3 & K);

/// Export 3D point vector and camera position to PLY format
bool exportToPly(const std::vector<Vec3> & vec_points,
  const std::vector<Vec3> & vec_camPos,
  const std::string & sFileName);

int main() {

  const std::string sInputDir = stlplus::folder_up(string(THIS_SOURCE_DIR))
    + "/imageData/SceauxCastle/";
  const string jpg_filenameL = sInputDir + "100_7101.jpg";
  const string jpg_filenameR = sInputDir + "100_7102.jpg";

  Image<unsigned char> imageL, imageR;
  ReadImage(jpg_filenameL.c_str(), &imageL);
  ReadImage(jpg_filenameR.c_str(), &imageR);

  //--
  // Detect regions thanks to an image_describer
  //--
  using namespace aliceVision::feature;
  std::unique_ptr<ImageDescriber> image_describer(new ImageDescriber_SIFT);
  std::map<IndexT, std::unique_ptr<feature::Regions> > regions_perImage;
  image_describer->Describe(imageL, regions_perImage[0]);
  image_describer->Describe(imageR, regions_perImage[1]);

  const SIFT_Regions* regionsL = dynamic_cast<SIFT_Regions*>(regions_perImage.at(0).get());
  const SIFT_Regions* regionsR = dynamic_cast<SIFT_Regions*>(regions_perImage.at(1).get());

  const PointFeatures
    featsL = regions_perImage.at(0)->GetRegionsPositions(),
    featsR = regions_perImage.at(1)->GetRegionsPositions();

  // Show both images side by side
  {
    Image<unsigned char> concat;
    ConcatH(imageL, imageR, concat);
    string out_filename = "01_concat.jpg";
    WriteImage(out_filename.c_str(), concat);
  }

  //- Draw features on the two image (side by side)
  {
    Image<unsigned char> concat;
    ConcatH(imageL, imageR, concat);

    //-- Draw features :
    for (size_t i=0; i < featsL.size(); ++i )  {
      const SIOPointFeature point = regionsL->Features()[i];
      DrawCircle(point.x(), point.y(), point.scale(), 255, &concat);
    }
    for (size_t i=0; i < featsR.size(); ++i )  {
      const SIOPointFeature point = regionsR->Features()[i];
      DrawCircle(point.x()+imageL.Width(), point.y(), point.scale(), 255, &concat);
    }
    string out_filename = "02_features.jpg";
    WriteImage(out_filename.c_str(), concat);
  }

  std::vector<IndMatch> vec_PutativeMatches;
  //-- Perform matching -> find Nearest neighbor, filtered with Distance ratio
  {
    // Find corresponding points
    matching::DistanceRatioMatch(
      0.8, matching::BRUTE_FORCE_L2,
      *regions_perImage.at(0).get(),
      *regions_perImage.at(1).get(),
      vec_PutativeMatches);

    IndMatchDecorator<float> matchDeduplicator(
            vec_PutativeMatches, featsL, featsR);
    matchDeduplicator.getDeduplicated(vec_PutativeMatches);

    std::cout
      << regions_perImage.at(0)->RegionCount() << " #Features on image A" << std::endl
      << regions_perImage.at(1)->RegionCount() << " #Features on image B" << std::endl
      << vec_PutativeMatches.size() << " #matches with Distance Ratio filter" << std::endl;

    // Draw correspondences after Nearest Neighbor ratio filter
    svgDrawer svgStream( imageL.Width() + imageR.Width(), max(imageL.Height(), imageR.Height()));
    svgStream.drawImage(jpg_filenameL, imageL.Width(), imageL.Height());
    svgStream.drawImage(jpg_filenameR, imageR.Width(), imageR.Height(), imageL.Width());
    for (size_t i = 0; i < vec_PutativeMatches.size(); ++i) {
      //Get back linked feature, draw a circle and link them by a line
      const SIOPointFeature L = regionsL->Features()[vec_PutativeMatches[i]._i];
      const SIOPointFeature R = regionsR->Features()[vec_PutativeMatches[i]._j];
      svgStream.drawLine(L.x(), L.y(), R.x()+imageL.Width(), R.y(), svgStyle().stroke("green", 2.0));
      svgStream.drawCircle(L.x(), L.y(), L.scale(), svgStyle().stroke("yellow", 2.0));
      svgStream.drawCircle(R.x()+imageL.Width(), R.y(), R.scale(),svgStyle().stroke("yellow", 2.0));
    }
    const std::string out_filename = "03_siftMatches.svg";
    std::ofstream svgFile( out_filename.c_str() );
    svgFile << svgStream.closeSvgFile().str();
    svgFile.close();
  }

  // Essential geometry filtering of putative matches
  {
    Mat3 K;
    //read K from file
    if (!readIntrinsic(stlplus::create_filespec(sInputDir,"K","txt"), K))
    {
      std::cerr << "Cannot read intrinsic parameters." << std::endl;
      return EXIT_FAILURE;
    }

    //A. prepare the corresponding putatives points
    Mat xL(2, vec_PutativeMatches.size());
    Mat xR(2, vec_PutativeMatches.size());
    for (size_t k = 0; k < vec_PutativeMatches.size(); ++k)  {
      const PointFeature & imaL = featsL[vec_PutativeMatches[k]._i];
      const PointFeature & imaR = featsR[vec_PutativeMatches[k]._j];
      xL.col(k) = imaL.coords().cast<double>();
      xR.col(k) = imaR.coords().cast<double>();
    }

    //B. Compute the relative pose thanks to a essential matrix estimation
    std::pair<size_t, size_t> size_imaL(imageL.Width(), imageL.Height());
    std::pair<size_t, size_t> size_imaR(imageR.Width(), imageR.Height());
    sfm::RelativePoseInfo relativePose_info;
    if (!sfm::robustRelativePose(K, K, xL, xR, relativePose_info, size_imaL, size_imaR, 256))
    {
      std::cerr << " /!\\ Robust relative pose estimation failure."
        << std::endl;
      return EXIT_FAILURE;
    }

    std::cout << "\nFound an Essential matrix:\n"
      << "\tprecision: " << relativePose_info.found_residual_precision << " pixels\n"
      << "\t#inliers: " << relativePose_info.vec_inliers.size() << "\n"
      << "\t#matches: " << vec_PutativeMatches.size()
      << std::endl;

    // Show Essential validated point
    svgDrawer svgStream( imageL.Width() + imageR.Width(), max(imageL.Height(), imageR.Height()));
    svgStream.drawImage(jpg_filenameL, imageL.Width(), imageL.Height());
    svgStream.drawImage(jpg_filenameR, imageR.Width(), imageR.Height(), imageL.Width());
    for (size_t i = 0; i < relativePose_info.vec_inliers.size(); ++i)  {
      const SIOPointFeature & LL = regionsL->Features()[vec_PutativeMatches[relativePose_info.vec_inliers[i]]._i];
      const SIOPointFeature & RR = regionsR->Features()[vec_PutativeMatches[relativePose_info.vec_inliers[i]]._j];
      const Vec2f L = LL.coords();
      const Vec2f R = RR.coords();
      svgStream.drawLine(L.x(), L.y(), R.x()+imageL.Width(), R.y(), svgStyle().stroke("green", 2.0));
      svgStream.drawCircle(L.x(), L.y(), LL.scale(), svgStyle().stroke("yellow", 2.0));
      svgStream.drawCircle(R.x()+imageL.Width(), R.y(), RR.scale(),svgStyle().stroke("yellow", 2.0));
    }
    const std::string out_filename = "04_ACRansacEssential.svg";
    std::ofstream svgFile( out_filename.c_str() );
    svgFile << svgStream.closeSvgFile().str();
    svgFile.close();

    //C. Triangulate and export inliers as a PLY scene
    std::vector<Vec3> vec_3DPoints;

    // Setup camera intrinsic and poses
    Pinhole intrinsic0(imageL.Width(), imageL.Height(), K(0, 0), K(0, 2), K(1, 2));
    Pinhole intrinsic1(imageR.Width(), imageR.Height(), K(0, 0), K(0, 2), K(1, 2));

    const Pose3 pose0 = Pose3(Mat3::Identity(), Vec3::Zero());
    const Pose3 pose1 = relativePose_info.relativePose;

    // Init structure by inlier triangulation
    const Mat34 P1 = intrinsic0.get_projective_equivalent(pose0);
    const Mat34 P2 = intrinsic1.get_projective_equivalent(pose1);
    std::vector<double> vec_residuals;
    vec_residuals.reserve(relativePose_info.vec_inliers.size() * 4);
    for (size_t i = 0; i < relativePose_info.vec_inliers.size(); ++i)  {
      const SIOPointFeature & LL = regionsL->Features()[vec_PutativeMatches[relativePose_info.vec_inliers[i]]._i];
      const SIOPointFeature & RR = regionsR->Features()[vec_PutativeMatches[relativePose_info.vec_inliers[i]]._j];
      // Point triangulation
      Vec3 X;
      TriangulateDLT(P1, LL.coords().cast<double>(), P2, RR.coords().cast<double>(), &X);
      // Reject point that is behind the camera
      if (pose0.depth(X) < 0 && pose1.depth(X) < 0)
        continue;

      const Vec2 residual0 = intrinsic0.residual(pose0, X, LL.coords().cast<double>());
      const Vec2 residual1 = intrinsic1.residual(pose1, X, RR.coords().cast<double>());
      vec_residuals.push_back(fabs(residual0(0)));
      vec_residuals.push_back(fabs(residual0(1)));
      vec_residuals.push_back(fabs(residual1(0)));
      vec_residuals.push_back(fabs(residual1(1)));
      vec_3DPoints.emplace_back(X);
    }

    // Display some statistics of reprojection errors
    MinMaxMeanMedian<float> stats(vec_residuals.begin(), vec_residuals.end());

    std::cout << std::endl
      << "Triangulation residuals statistics:" << "\n"
      << "\t-- Residual min:\t" << stats.min << "\n"
      << "\t-- Residual median:\t" << stats.median << "\n"
      << "\t-- Residual max:\t " << stats.max << "\n"
      << "\t-- Residual mean:\t " << stats.mean << std::endl;

      // Export as PLY (camera pos + 3Dpoints)
      std::vector<Vec3> vec_camPos;
      vec_camPos.push_back( pose0.center() );
      vec_camPos.push_back( pose1.center() );
      exportToPly(vec_3DPoints, vec_camPos, "EssentialGeometry.ply");
  }
  return EXIT_SUCCESS;
}

bool readIntrinsic(const std::string & fileName, Mat3 & K)
{
  // Load the K matrix
  ifstream in;
  in.open( fileName.c_str(), ifstream::in);
  if(in.is_open())  {
    for (int j=0; j < 3; ++j)
      for (int i=0; i < 3; ++i)
        in >> K(j,i);
  }
  else  {
    std::cerr << std::endl
      << "Invalid input K.txt file" << std::endl;
    return false;
  }
  return true;
}

/// Export 3D point vector and camera position to PLY format
bool exportToPly(const std::vector<Vec3> & vec_points,
  const std::vector<Vec3> & vec_camPos,
  const std::string & sFileName)
{
  std::ofstream outfile;
  outfile.open(sFileName.c_str(), std::ios_base::out);

  outfile << "ply"
    << '\n' << "format ascii 1.0"
    << '\n' << "element vertex " << vec_points.size()+vec_camPos.size()
    << '\n' << "property float x"
    << '\n' << "property float y"
    << '\n' << "property float z"
    << '\n' << "property uchar red"
    << '\n' << "property uchar green"
    << '\n' << "property uchar blue"
    << '\n' << "end_header" << std::endl;

  for (size_t i=0; i < vec_points.size(); ++i)  {
      outfile << vec_points[i].transpose()
      << " 255 255 255" << "\n";
  }

  for (size_t i=0; i < vec_camPos.size(); ++i)  {
    outfile << vec_camPos[i].transpose()
      << " 0 255 0" << "\n";
  }
  outfile.flush();
  bool bOk = outfile.good();
  outfile.close();
  return bOk;
}
