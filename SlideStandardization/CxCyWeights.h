//===========================================================================
// This class is for refining the rigid transfromation of CxCy values
// The weights are applied based on the amount of stain for each pixel
//===========================================================================
#ifndef __CXYWEIGHTS_H__
#define __CXYWEIGHTS_H__


#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/ml/ml.hpp>

/// <Summary>
/// This namespace is for refining the rigid transformation of CxCy values
/// The weights are applied based on the amount of stain for each pixel
/// </summary>
namespace CxCyWeights
{
	struct Weights
	{
		cv::Mat hema;
		cv::Mat eosin;
		cv::Mat background;
	};

	// applies the already calculated weights on the transformes CxCy values
	cv::Mat ApplyWeights(cv::Mat& c_xy_hema, cv::Mat& c_xy_eosin, cv::Mat& c_xy_background, Weights& weights);

	/// <summary>
	/// Creates and trains a NaiveBayesClassifier.
	/// </summary>
	cv::Ptr<cv::ml::NormalBayesClassifier> CreateNaiveBayesClassifier(cv::Mat& c_x, cv::Mat& c_y, cv::Mat& density, cv::Mat& all_tissue_classes);

	// Generates weights for the case that test data of Cx,Cy,D are different from training data
	// This is in particular used for the case of generating waits for Look up table values
	Weights GenerateWeights(cv::Mat& c_x, cv::Mat& c_y, cv::Mat& density, cv::Ptr<cv::ml::NormalBayesClassifier>& classifier);
};
#endif // __CXYWEIGHTS_H__