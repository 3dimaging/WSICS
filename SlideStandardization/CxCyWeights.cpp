#include "CxCyWeights.h"

namespace CxCyWeights
{
	cv::Mat ApplyWeights(cv::Mat& c_xy_hema, cv::Mat& c_xy_eosin, cv::Mat& c_xy_background, Weights& weights)
	{
		cv::Mat c_xy_normalized(c_xy_hema.rows, 2, CV_32FC1);

		size_t counter = 0;
		for (size_t row = 0; row < weights.hema.rows; ++row)
		{
			for (size_t col = 0; col < weights.hema.cols; ++col)
			{
				c_xy_normalized.at<float>(counter, 0) = c_xy_hema.at<float>(counter, 0) * weights.hema.at<float>(row, col) + c_xy_eosin.at<float>(counter, 0) * weights.eosin.at<float>(row, col) + c_xy_background.at<float>(counter, 0) * weights.background.at<float>(row, col);
				c_xy_normalized.at<float>(counter, 1) = c_xy_hema.at<float>(counter, 1) * weights.hema.at<float>(row, col) + c_xy_eosin.at<float>(counter, 1) * weights.eosin.at<float>(row, col) + c_xy_background.at<float>(counter, 1) * weights.background.at<float>(row, col);
				++counter;
			}
		}

		return c_xy_normalized;
	}

	cv::Ptr<cv::ml::NormalBayesClassifier> CreateNaiveBayesClassifier(cv::Mat& c_x, cv::Mat& c_y, cv::Mat& density, cv::Mat& class_data)
	{
		cv::Mat samples(class_data.rows *  class_data.cols, 3, CV_32F);
		cv::Mat responses(class_data.rows *  class_data.cols, 3, CV_32F);
		size_t current_sample = 0;
		for (size_t row = 0; row < class_data.rows; ++row)
		{
			for (size_t col = 0; col < class_data.cols; ++col)
			{
				samples.at<float>(current_sample, 0) = c_x.at<float>(row, col);
				samples.at<float>(current_sample, 1) = c_y.at<float>(row, col);
				samples.at<float>(current_sample, 2) = density.at<float>(row, col);

				switch (class_data.at<int>(row, col))
				{
				case 1: responses.at<float>(current_sample, 0) = 0; break;
				case 2: responses.at<float>(current_sample, 0) = 1; break;
				case 3: responses.at<float>(current_sample, 0) = 2; break;
				}

				++current_sample;
			}
		}

		cv::Ptr<cv::ml::TrainData> train_data(cv::ml::TrainData::create(samples, cv::ml::COL_SAMPLE, responses));
		cv::Ptr<cv::ml::NormalBayesClassifier> normal_bayes_classifier(cv::ml::NormalBayesClassifier::create());
		normal_bayes_classifier->train(train_data);

		return normal_bayes_classifier;
	}

	Weights GenerateWeights(cv::Mat& c_x, cv::Mat& c_y, cv::Mat& density, cv::Ptr<cv::ml::NormalBayesClassifier>& classifier)
	{
		cv::Mat classifier_input(cv::Mat::zeros(c_x.rows, 3, CV_32F));
		for (size_t row = 0; row < c_x.rows; ++row)
		{
			classifier_input.at<float>(row, 0) = c_x.at<float>(row, 0);
			classifier_input.at<float>(row, 1) = c_y.at<float>(row, 0);
			classifier_input.at<float>(row, 2) = density.at<float>(row, 0);
		}

		cv::Mat output;
		cv::Mat probability_output;
		classifier->predictProb(classifier_input, output, probability_output);

		Weights weights { cv::Mat::zeros(c_x.rows, c_x.cols, CV_32FC1), cv::Mat::zeros(c_x.rows, c_x.cols, CV_32FC1), cv::Mat::zeros(c_x.rows, c_x.cols, CV_32FC1) };
		for (size_t row = 0; row < c_x.rows; ++row)
		{
			weights.hema.at<float>(row, 0)			= probability_output.at<float>(row, 0);
			weights.eosin.at<float>(row, 0)			= probability_output.at<float>(row, 1);
			weights.background.at<float>(row, 0)	= probability_output.at<float>(row, 2);
		}

		return weights;
	}
}